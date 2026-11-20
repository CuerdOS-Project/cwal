/*
 * cwal - Pywal clone in C
 * Copyright (c) 2026 - Ported from Python by Alecaishere/CuerdOS Dev. Team
 *
 * templates.c - pywal-compatible template engine.
 *
 * Reads template files (from the built-in templates/ dir AND from
 * $XDG_CONFIG_HOME/wal/templates/), substitutes placeholders of the
 * form  {key}, {key.attr}, {key.method(args)}  and writes the result
 * to the cache dir, mirroring pywal/export.py.
 *
 * Literal braces are escaped with  {{  and  }}  (same as Python's
 * str.format), so e.g. CSS ":root {{ ... }}" becomes ":root { ... }".
 *
 * Supported keys:   wallpaper, alpha, background, foreground, cursor,
 *                   color0..color15
 *
 * Supported attrs (mirrors util.Color in pywal/util.py):
 *   .strip          "RRGGBB"           (no leading #)
 *   .rgb            "R,G,B"
 *   .xrgba          "rr/rr/rr/ff"      (xrdb rgba)
 *   .rgba           "rgba(R,G,B,A)"    (A = alpha_dec)
 *   .alpha          "[NN]#RRGGBB"      (URxvt alpha)
 *   .alpha_dec      "0.NN"             (e.g. "1.0", "0.95")
 *   .decimal        "#4278190080"
 *   .decimal_strip  "4278190080"
 *   .octal          "#o..."
 *   .octal_strip    "o..."
 *   .red            "0.XXX"            (3 decimal places)
 *   .green          "0.XXX"
 *   .blue           "0.XXX"
 *
 * Supported methods (pywal Color.lighten/darken/saturate):
 *   .lighten(P)     -> "#RRGGBB" of the color lightened by P%
 *   .darken(P)      -> "#RRGGBB" of the color darkened by P%
 *   .saturate(P)    -> "#RRGGBB" of the color with saturation set to P%
 *
 * For wallpaper/alpha keys, only the bare {wallpaper} / {alpha} forms
 * are supported (their values are strings, not Color objects).
 */
#include "cwal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>

/* ---------- dynamic string builder ---------- */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} sbuf_t;

static int sbuf_reserve(sbuf_t *s, size_t extra)
{
    if (s->len + extra + 1 > s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 256;
        while (nc < s->len + extra + 1) nc *= 2;
        char *nd = realloc(s->data, nc);
        if (!nd) return -1;
        s->data = nd;
        s->cap  = nc;
    }
    return 0;
}

static void sbuf_putc(sbuf_t *s, char c)
{
    if (sbuf_reserve(s, 1) == 0) s->data[s->len++] = c;
}

static void sbuf_puts(sbuf_t *s, const char *str)
{
    size_t n = strlen(str);
    if (sbuf_reserve(s, n) == 0) {
        memcpy(s->data + s->len, str, n);
        s->len += n;
    }
}

/* ---------- color value formatting (pywal Color semantics) ---------- */

/* Look up a base value for a placeholder key.
 * Returns:
 *   1 = hex color (written into hex_out, 8 bytes)
 *   2 = plain string (wallpaper path / alpha)
 *   0 = unknown key (out untouched) */
static int lookup_key(const palette_t *pal, const char *key,
                      hex_t hex_out, char *str_out, size_t str_cap)
{
    if (strcmp(key, "wallpaper") == 0) {
        snprintf(str_out, str_cap, "%s", pal->wallpaper);
        return 2;
    }
    if (strcmp(key, "alpha") == 0) {
        snprintf(str_out, str_cap, "%s", pal->alpha);
        return 2;
    }
    if (strcmp(key, "background") == 0) {
        memcpy(hex_out, pal->background, sizeof(hex_t));
        return 1;
    }
    if (strcmp(key, "foreground") == 0) {
        memcpy(hex_out, pal->foreground, sizeof(hex_t));
        return 1;
    }
    if (strcmp(key, "cursor") == 0) {
        memcpy(hex_out, pal->cursor, sizeof(hex_t));
        return 1;
    }
    for (int i = 0; i < 16; i++) {
        char name[16];
        snprintf(name, sizeof(name), "color%d", i);
        if (strcmp(key, name) == 0) {
            memcpy(hex_out, pal->colors[i], sizeof(hex_t));
            return 1;
        }
    }
    return 0;
}

/* Format a Python-style float string: 1.0 -> "1.0", 0.9 -> "0.9",
 * 0.5 -> "0.5", 0.95 -> "0.95". Matches str(int(x)/100) in CPython. */
static void py_float(double v, char *out, size_t cap)
{
    if (v == (double)(long long)v) {
        snprintf(out, cap, "%lld.0", (long long)v);
    } else {
        snprintf(out, cap, "%g", v);
    }
}

/* Format a color attribute into a temporary buffer.
 * `hex` is the source color (#RRGGBB), `alpha` is the alpha string
 * ("100" by default). `attr` is e.g. "strip", "rgb", "lighten(20)". */
static void format_attr(const char *hex, const char *alpha,
                        const char *attr, sbuf_t *out)
{
    rgb_t c = hex_to_rgb(hex);
    char  buf[128];

    /* method call: attr ends with ')' */
    if (attr[strlen(attr) - 1] == ')') {
        char name[32] = {0};
        char argbuf[32] = {0};
        const char *lp = strchr(attr, '(');
        if (!lp) { sbuf_puts(out, ""); return; }
        size_t n = (size_t)(lp - attr);
        if (n >= sizeof(name)) n = sizeof(name) - 1;
        memcpy(name, attr, n);
        const char *rp = strchr(lp + 1, ')');
        if (rp) {
            size_t an = (size_t)(rp - lp - 1);
            if (an >= sizeof(argbuf)) an = sizeof(argbuf) - 1;
            memcpy(argbuf, lp + 1, an);
        }
        /* parse arg: pywal uses regex [\D\.] to strip non-digits/dots
         * from the percent, then divides by 100. We just atof() and
         * divide by 100. */
        double pct = atof(argbuf) / 100.0;
        hex_t result;
        if (strcmp(name, "lighten") == 0) {
            lighten_color(hex, pct, &result);
        } else if (strcmp(name, "darken") == 0) {
            darken_color(hex, pct, &result);
        } else if (strcmp(name, "saturate") == 0) {
            saturate_color(hex, pct, &result);
        } else {
            sbuf_puts(out, "");
            return;
        }
        sbuf_puts(out, result);
        return;
    }

    /* plain attributes */
    if (strcmp(attr, "strip") == 0) {
        sbuf_puts(out, (hex[0] == '#') ? hex + 1 : hex);
    } else if (strcmp(attr, "rgb") == 0) {
        snprintf(buf, sizeof(buf), "%d,%d,%d", c.r, c.g, c.b);
        sbuf_puts(out, buf);
    } else if (strcmp(attr, "xrgba") == 0) {
        snprintf(buf, sizeof(buf), "%02x/%02x/%02x/ff", c.r, c.g, c.b);
        sbuf_puts(out, buf);
    } else if (strcmp(attr, "rgba") == 0) {
        int a = atoi(alpha);
        double a_dec = (a > 0 ? a : 100) / 100.0;
        char a_str[16];
        py_float(a_dec, a_str, sizeof(a_str));
        snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%s)",
                 c.r, c.g, c.b, a_str);
        sbuf_puts(out, buf);
    } else if (strcmp(attr, "alpha") == 0) {
        snprintf(buf, sizeof(buf), "[%s]%s", alpha, hex);
        sbuf_puts(out, buf);
    } else if (strcmp(attr, "alpha_dec") == 0) {
        int a = atoi(alpha);
        double a_dec = (a > 0 ? a : 100) / 100.0;
        py_float(a_dec, buf, sizeof(buf));
        sbuf_puts(out, buf);
    } else if (strcmp(attr, "decimal") == 0) {
        unsigned v = ((unsigned)c.r << 16) | ((unsigned)c.g << 8) | c.b;
        snprintf(buf, sizeof(buf), "#%u", v);
        sbuf_puts(out, buf);
    } else if (strcmp(attr, "decimal_strip") == 0) {
        unsigned v = ((unsigned)c.r << 16) | ((unsigned)c.g << 8) | c.b;
        snprintf(buf, sizeof(buf), "%u", v);
        sbuf_puts(out, buf);
    } else if (strcmp(attr, "octal") == 0) {
        unsigned v = ((unsigned)c.r << 16) | ((unsigned)c.g << 8) | c.b;
        snprintf(buf, sizeof(buf), "#%o", v);
        sbuf_puts(out, buf);
    } else if (strcmp(attr, "octal_strip") == 0) {
        unsigned v = ((unsigned)c.r << 16) | ((unsigned)c.g << 8) | c.b;
        snprintf(buf, sizeof(buf), "%o", v);
        sbuf_puts(out, buf);
    } else if (strcmp(attr, "red") == 0) {
        snprintf(buf, sizeof(buf), "%.3f", c.r / 255.0);
        sbuf_puts(out, buf);
    } else if (strcmp(attr, "green") == 0) {
        snprintf(buf, sizeof(buf), "%.3f", c.g / 255.0);
        sbuf_puts(out, buf);
    } else if (strcmp(attr, "blue") == 0) {
        snprintf(buf, sizeof(buf), "%.3f", c.b / 255.0);
        sbuf_puts(out, buf);
    } else {
        /* unknown attribute: emit empty so the file is still valid */
        sbuf_puts(out, "");
    }
}

/* ---------- core substitution ---------- */

/* Substitute one placeholder starting at *p (which points just after
 * the '{'). Advances *p past the closing '}'. Returns 0 on success,
 * -1 on syntax error (caller emits the original text). */
static int substitute_one(const char **p, sbuf_t *out,
                          const palette_t *pal)
{
    const char *q = *p;
    /* find closing '}' (we don't support nested braces inside a
     * placeholder, which matches pywal's behavior) */
    const char *end = strchr(q, '}');
    if (!end) return -1;

    /* parse: key [ . attr ]  (attr may contain '(' and ')') */
    const char *dot = NULL;
    for (const char *c = q; c < end; c++) {
        if (*c == '.') { dot = c; break; }
    }

    char key[64];
    size_t klen = dot ? (size_t)(dot - q) : (size_t)(end - q);
    if (klen >= sizeof(key)) klen = sizeof(key) - 1;
    memcpy(key, q, klen);
    key[klen] = '\0';

    hex_t hex;
    char  strval[PATH_MAX];
    int   kind = lookup_key(pal, key, hex, strval, sizeof(strval));
    if (kind == 0) {
        /* unknown key -> leave the placeholder text as-is so the
         * user sees something is off; pywal would error out here. */
        sbuf_putc(out, '{');
        sbuf_puts(out, key);
        if (dot) {
            sbuf_putc(out, '.');
            sbuf_puts(out, dot + 1);
        }
        sbuf_putc(out, '}');
        *p = end + 1;
        return 0;
    }

    if (!dot) {
        /* bare {key}: hex colors print as "#RRGGBB", strings as-is */
        if (kind == 1) sbuf_puts(out, hex);
        else           sbuf_puts(out, strval);
    } else {
        /* attribute access -- only valid for hex colors */
        if (kind == 1) {
            size_t alen = (size_t)(end - dot - 1);
            char attr[64];
            if (alen >= sizeof(attr)) alen = sizeof(attr) - 1;
            memcpy(attr, dot + 1, alen);
            attr[alen] = '\0';
            format_attr(hex, pal->alpha, attr, out);
        } else {
            /* .attr on a string key (wallpaper.alpha?) -- emit value */
            sbuf_puts(out, strval);
        }
    }

    *p = end + 1;
    return 0;
}

char *template_substitute(const char *input, const palette_t *pal)
{
    sbuf_t s = {0};
    const char *p = input;
    while (*p) {
        if (*p == '{') {
            if (p[1] == '{') {
                sbuf_putc(&s, '{');
                p += 2;
                continue;
            }
            const char *after = p + 1;
            if (substitute_one(&after, &s, pal) == 0) {
                p = after;
            } else {
                /* no closing brace, emit literally */
                sbuf_putc(&s, *p++);
            }
        } else if (*p == '}') {
            if (p[1] == '}') {
                sbuf_putc(&s, '}');
                p += 2;
                continue;
            }
            sbuf_putc(&s, *p++);
        } else {
            sbuf_putc(&s, *p++);
        }
    }
    /* null-terminate */
    sbuf_putc(&s, '\0');
    if (s.data && s.len > 0) s.data[s.len - 1] = '\0';
    return s.data;
}

/* ---------- file processing ---------- */

static char *read_file_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    return buf;
}

int template_process_file(const char *in_path, const char *out_path,
                          const palette_t *pal)
{
    char *text = read_file_text(in_path);
    if (!text) {
        fprintf(stderr, "cwal: cannot read template '%s'\n", in_path);
        return 1;
    }
    char *result = template_substitute(text, pal);
    free(text);
    if (!result) return 1;

    /* ensure parent dir exists */
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", out_path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; ensure_dir(dir); }

    FILE *f = fopen(out_path, "w");
    if (!f) {
        fprintf(stderr, "cwal: cannot write '%s': %s\n",
                out_path, strerror(errno));
        free(result);
        return 1;
    }
    fputs(result, f);
    fclose(f);
    free(result);
    return 0;
}

/* Walk a directory and call template_process_file for every regular
 * file. Returns number of files processed. */
static int process_dir(const char *tpl_dir, const char *out_dir,
                       const palette_t *pal)
{
    DIR *d = opendir(tpl_dir);
    if (!d) return 0;

    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        /* skip editor swap files */
        size_t nl = strlen(e->d_name);
        if (nl > 4 && strcmp(e->d_name + nl - 4, ".swp") == 0) continue;
        if (nl > 4 && strcmp(e->d_name + nl - 4, ".swo") == 0) continue;
        if (strcmp(e->d_name, ".DS_Store") == 0) continue;

        char in_path[PATH_MAX], out_path[PATH_MAX];
        snprintf(in_path,  sizeof(in_path),  "%s/%s", tpl_dir,  e->d_name);
        snprintf(out_path, sizeof(out_path), "%s/%s", out_dir, e->d_name);

        struct stat st;
        if (stat(in_path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        if (template_process_file(in_path, out_path, pal) == 0) count++;
    }
    closedir(d);
    return count;
}

int template_every(const palette_t *pal, const char *cache_dir,
                   const char *builtin_dir, const char *user_dir)
{
    int n = 0;
    if (builtin_dir && *builtin_dir) {
        n += process_dir(builtin_dir, cache_dir, pal);
    }
    if (user_dir && *user_dir) {
        ensure_dir(user_dir);
        n += process_dir(user_dir, cache_dir, pal);
    }
    return n;
}
