/*
 * cwal - Pywal clone in C
 * Copyright (c) 2026 - Ported from Python by Alecaishere/CuerdOS Dev. Team
 *
 * cache.c - JSON colors.json save/load, cache filename, dir helpers.
 *
 * The on-disk format matches pywal's so files are interchangeable:
 *   {
 *     "wallpaper": "/path/to/img.jpg",
 *     "alpha": "100",
 *     "special": { "background": "#...", "foreground": "#...",
 *                  "cursor": "#..." },
 *     "colors":   { "color0": "#...", ..., "color15": "#..." }
 *   }
 *
 * Loading is done with a tiny hand-written line scanner (the schema
 * is fixed and known, so a real JSON parser would be overkill).
 */
#include "cwal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>

/* ---------- path helpers ---------- */

int ensure_dir(const char *path)
{
    if (!path || !*path) return 0;
    /* walk the path creating each component as needed */
    char buf[PATH_MAX];
    snprintf(buf, sizeof(buf), "%s", path);

    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
                /* ignore */
            }
            *p = '/';
        }
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
        /* not fatal -- might already exist */
    }
    return 0;
}

/* pywal: re.sub("[/|\\|.]", "_", img) */
static void sanitize_img_name(const char *img, char *out, size_t cap)
{
    size_t i = 0;
    for (const char *p = img; *p && i + 1 < cap; p++) {
        char c = *p;
        if (c == '/' || c == '|' || c == '\\' || c == '.') c = '_';
        out[i++] = c;
    }
    out[i] = '\0';
}

int cache_fname(const char *img, const char *backend, bool light,
                const char *sat, const char *cache_dir, char *out)
{
    struct stat st;
    if (stat(img, &st) != 0) {
        fprintf(stderr, "cwal: stat('%s') failed: %s\n",
                img, strerror(errno));
        return 1;
    }
    char name[256];
    sanitize_img_name(img, name, sizeof(name));

    const char *bri = light ? "light" : "dark";
    const char *s   = (sat && *sat) ? sat : "";

    snprintf(out, PATH_MAX, "%s/schemes/%s_%s_%s_%s_%lld_%s.json",
             cache_dir, name, bri, backend, s,
             (long long)st.st_size, CWAL_CACHE_VERSION);
    return 0;
}

/* ---------- JSON save ---------- */

static void write_hex_kv(FILE *f, const char *key, const char *val,
                         int indent, bool last)
{
    for (int i = 0; i < indent; i++) fputc(' ', f);
    fprintf(f, "\"%s\": \"%s\"%s\n", key, val, last ? "" : ",");
}

int cache_save_json(const palette_t *pal, const char *path)
{
    /* ensure parent dir exists */
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        ensure_dir(dir);
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "cwal: cannot write '%s': %s\n",
                path, strerror(errno));
        return 1;
    }

    fprintf(f, "{\n");
    fprintf(f, "    \"wallpaper\": \"%s\",\n", pal->wallpaper);
    fprintf(f, "    \"alpha\": \"%s\",\n", pal->alpha);
    fprintf(f, "    \"special\": {\n");
    write_hex_kv(f, "background", pal->background, 8, false);
    write_hex_kv(f, "foreground", pal->foreground, 8, false);
    write_hex_kv(f, "cursor",     pal->cursor,     8, true);
    fprintf(f, "    },\n");
    fprintf(f, "    \"colors\": {\n");
    for (int i = 0; i < 16; i++) {
        char key[16];
        snprintf(key, sizeof(key), "color%d", i);
        write_hex_kv(f, key, pal->colors[i], 8, i == 15);
    }
    fprintf(f, "    }\n");
    fprintf(f, "}\n");

    fclose(f);
    return 0;
}

/* ---------- JSON load (line scanner) ---------- */

/* Copy a hex/alpha value into a fixed-size destination, clamping to
 * dstcap-1 chars. Used to silence -Wformat-truncation without lying
 * about the actual source length. */
static void copy_clamped(char *dst, size_t dstcap, const char *src)
{
    if (dstcap == 0) return;
    size_t i = 0;
    while (src[i] && i + 1 < dstcap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* Extract the value of "key": "value" from a line. Returns 0 on
 * success and writes the value (without quotes) into out. */
static int extract_kv(const char *line, const char *key,
                      char *out, size_t cap)
{
    /* find "key" */
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(line, pat);
    if (!p) return 1;
    p += strlen(pat);
    /* skip whitespace and colon */
    while (*p && (*p == ' ' || *p == '\t')) p++;
    if (*p != ':') return 1;
    p++;
    while (*p && (*p == ' ' || *p == '\t')) p++;
    if (*p != '"') return 1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < cap) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 0;
}

int cache_load_json(const char *path, palette_t *pal)
{
    FILE *f = fopen(path, "r");
    if (!f) return 1;
    memset(pal, 0, sizeof(*pal));

    char line[512];
    /* default alpha if missing */
    copy_clamped(pal->alpha, sizeof(pal->alpha), "100");
    copy_clamped(pal->wallpaper, sizeof(pal->wallpaper), "None");

    while (fgets(line, sizeof(line), f)) {
        char val[64];
        if (extract_kv(line, "wallpaper", val, sizeof(val)) == 0) {
            copy_clamped(pal->wallpaper, sizeof(pal->wallpaper), val);
        } else if (extract_kv(line, "alpha", val, sizeof(val)) == 0) {
            copy_clamped(pal->alpha, sizeof(pal->alpha), val);
        } else if (extract_kv(line, "background", val, sizeof(val)) == 0) {
            copy_clamped(pal->background, sizeof(hex_t), val);
        } else if (extract_kv(line, "foreground", val, sizeof(val)) == 0) {
            copy_clamped(pal->foreground, sizeof(hex_t), val);
        } else if (extract_kv(line, "cursor", val, sizeof(val)) == 0) {
            copy_clamped(pal->cursor, sizeof(hex_t), val);
        } else {
            /* try color0..color15 */
            for (int i = 0; i < 16; i++) {
                char key[16];
                snprintf(key, sizeof(key), "color%d", i);
                if (extract_kv(line, key, val, sizeof(val)) == 0) {
                    copy_clamped(pal->colors[i], sizeof(hex_t), val);
                    break;
                }
            }
        }
    }

    /* if special.* missing, derive from colors[] */
    if (pal->background[0] == '\0' || pal->background[0] != '#') {
        memcpy(pal->background, pal->colors[0], sizeof(hex_t));
    }
    if (pal->foreground[0] != '#') {
        memcpy(pal->foreground, pal->colors[15], sizeof(hex_t));
    }
    if (pal->cursor[0] != '#') {
        memcpy(pal->cursor, pal->colors[15], sizeof(hex_t));
    }

    fclose(f);
    return 0;
}

/* ---------- raw sequences blob ---------- */

int cache_save_sequences(const char *blob, const char *path)
{
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; ensure_dir(dir); }

    FILE *f = fopen(path, "w");
    if (!f) return 1;
    fputs(blob, f);
    fclose(f);
    return 0;
}
