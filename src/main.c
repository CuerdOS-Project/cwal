/*
 * cwal - Pywal clone in C
 * Copyright (c) 2026 - Ported from Python by Alecaishere/CuerdOS Dev. Team
 *
 * main.c - cwal CLI entry point.
 *
 * C port of pywal. Supports:
 *   -i IMG          image to derive colors from
 *   -l              light theme
 *   -b BG           override background color
 *   -a ALPHA        terminal alpha (e.g. "90" for URxvt)
 *   --backend NAME  accepted for compatibility (always "kmeans")
 *   --saturate AMT  extra saturation 0.0..1.0
 *   --vte           VTE escape fix
 *   -s              skip sending colors to terminals
 *   --skip-export   skip the template export step
 *   -q              quiet
 *   -n              no-op (we never set the wallpaper)
 *   -c              delete cached schemes
 *   -R              restore previous colorscheme from colors.json
 *   --preview       print the palette of the last colorscheme
 *   -v              version
 *
 * Anything pywal-specific that we don't implement (theme listing,
 * reload of gtk/i3/xrdb, -o script hooks) is intentionally absent.
 */
#include "cwal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <sys/stat.h>
#include <errno.h>

/* Built-in templates dir. Override at build time with
 *   make TEMPLATE_DIR=/path/to/templates
 * If empty, we fall back to ./templates/ in the current directory
 * (handy for running from the source tree). */
#ifndef CWAL_TEMPLATE_DIR
#define CWAL_TEMPLATE_DIR "/usr/local/share/cwal/templates"
#endif

static void usage(FILE *f)
{
    fprintf(f,
        "cwal %s - Generate colorschemes on the fly (C port of pywal)\n"
        "\n"
        "USAGE:\n"
        "  cwal -i IMAGE [-l] [-b BG] [-a ALPHA] [--saturate AMT] [--vte]\n"
        "  cwal -R              Restore last colorscheme\n"
        "  cwal --preview       Show palette of last colorscheme\n"
        "  cwal -c              Delete cached schemes\n"
        "  cwal -v              Print version\n"
        "\n"
        "OPTIONS:\n"
        "  -i, --image IMG      Image to derive palette from\n"
        "  -l, --light          Generate a light colorscheme\n"
        "  -b, --background BG  Override the background color\n"
        "  -a, --alpha ALPHA    Terminal background transparency (URxvt)\n"
        "      --backend NAME   Accepted (always 'kmeans' in this build)\n"
        "      --saturate AMT   Extra saturation in [0.0, 1.0]\n"
        "      --vte            Work around VTE escape quirks\n"
        "  -s, --skip-term      Don't push colors to terminals\n"
        "      --skip-export   Skip the template export step\n"
        "  -n                   No-op (wallpaper is never set)\n"
        "  -q, --quiet          Suppress informational output\n"
        "  -c, --clean-cache    Delete all cached colorschemes\n"
        "  -R, --restore        Restore previous colorscheme\n"
        "      --preview        Print the palette of the last colorscheme\n"
        "  -v, --version        Print version and exit\n"
        "  -h, --help           Show this help\n",
        CWAL_VERSION);
}

static void resolve_cache_dir(char *out, size_t cap)
{
    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    if (!home) home = getenv("USERPROFILE");
    if (xdg && *xdg) {
        snprintf(out, cap, "%s/wal", xdg);
    } else if (home && *home) {
        snprintf(out, cap, "%s/.cache/wal", home);
    } else {
        snprintf(out, cap, "/tmp/wal");
    }
}

static void resolve_conf_dir(char *out, size_t cap)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (!home) home = getenv("USERPROFILE");
    if (xdg && *xdg) {
        snprintf(out, cap, "%s/wal", xdg);
    } else if (home && *home) {
        snprintf(out, cap, "%s/.config/wal", home);
    } else {
        snprintf(out, cap, "/tmp/cwal-conf");
    }
}

/* Pick the built-in templates dir. Priority:
 *   1. $CWAL_TEMPLATES_DIR  (env override)
 *   2. ./templates/         (exists in the source tree)
 *   3. CWAL_TEMPLATE_DIR    (compile-time install path)
 */
static void resolve_template_dir(char *out, size_t cap)
{
    const char *env = getenv("CWAL_TEMPLATES_DIR");
    if (env && *env) {
        snprintf(out, cap, "%s", env);
        return;
    }
    struct stat st;
    if (stat("./templates", &st) == 0 && S_ISDIR(st.st_mode)) {
        snprintf(out, cap, "./templates");
        return;
    }
    snprintf(out, cap, "%s", CWAL_TEMPLATE_DIR);
}

/* Recursively rm -rf the schemes dir, ignoring errors. */
static int rm_rf_dir(const char *path)
{
    /* Use system() with rm -rf for simplicity and to avoid pulling
     * in nftw. Safe because we control the input path. */
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' 2>/dev/null", path);
    int rc = system(cmd);
    (void)rc;
    return 0;
}

int main(int argc, char **argv)
{
    const char *short_opts = "i:lqsnb:a:vRch";
    static const struct option long_opts[] = {
        {"image",      required_argument, 0, 'i'},
        {"light",      no_argument,       0, 'l'},
        {"quiet",      no_argument,       0, 'q'},
        {"skip-term",  no_argument,       0, 's'},
        {"background", required_argument, 0, 'b'},
        {"alpha",      required_argument, 0, 'a'},
        {"version",    no_argument,       0, 'v'},
        {"restore",    no_argument,       0, 'R'},
        {"clean-cache",no_argument,       0, 'c'},
        {"help",       no_argument,       0, 'h'},
        {"backend",    required_argument, 0, 1000},
        {"saturate",   required_argument, 0, 1001},
        {"vte",        no_argument,       0, 1002},
        {"preview",    no_argument,       0, 1003},
        {"skip-export",no_argument,       0, 1004},
        {0, 0, 0, 0}
    };

    const char *image = NULL;
    const char *bg_override = NULL;
    const char *alpha = "100";
    const char *backend = "kmeans";
    const char *sat = "";
    bool light = false;
    bool quiet = false;
    bool skip_term = false;
    bool vte_fix = false;
    bool restore = false;
    bool clean = false;
    bool preview = false;
    bool want_version = false;
    bool skip_export = false;

    int c;
    while ((c = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
        switch (c) {
            case 'i': image = optarg; break;
            case 'l': light = true; break;
            case 'q': quiet = true; break;
            case 's': skip_term = true; break;
            case 'n': break; /* no-op */
            case 'b': bg_override = optarg; break;
            case 'a': alpha = optarg; break;
            case 'v': want_version = true; break;
            case 'R': restore = true; break;
            case 'c': clean = true; break;
            case 'h': usage(stdout); return 0;
            case 1000: backend = optarg; break;
            case 1001: sat = optarg; break;
            case 1002: vte_fix = true; break;
            case 1003: preview = true; break;
            case 1004: skip_export = true; break;
            default:  usage(stderr); return 1;
        }
    }

    if (want_version) {
        printf("cwal %s\n", CWAL_VERSION);
        return 0;
    }

    char cache_dir[PATH_MAX];
    resolve_cache_dir(cache_dir, sizeof(cache_dir));

    if (clean) {
        char schemes[PATH_MAX];
        snprintf(schemes, sizeof(schemes), "%s/schemes", cache_dir);
        rm_rf_dir(schemes);
        if (!quiet) printf("cwal: cleared %s\n", schemes);
        return 0;
    }

    if (preview) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/colors.json", cache_dir);
        palette_t pal;
        if (cache_load_json(path, &pal) != 0) {
            fprintf(stderr, "cwal: no cached colorscheme at %s\n", path);
            return 1;
        }
        palette_print(&pal);
        return 0;
    }

    if (!image && !restore) {
        fprintf(stderr, "cwal: no input specified. Use -i IMG or -R.\n");
        usage(stderr);
        return 1;
    }

    palette_t pal;
    char cache_path[PATH_MAX];
    cache_path[0] = '\0';

    if (restore) {
        snprintf(cache_path, sizeof(cache_path), "%s/colors.json", cache_dir);
        if (cache_load_json(cache_path, &pal) != 0) {
            fprintf(stderr, "cwal: nothing to restore at %s\n", cache_path);
            return 1;
        }
        if (!quiet) printf("cwal: restored previous colorscheme.\n");
    } else {
        /* compute cache filename */
        if (cache_fname(image, backend, light, sat,
                        cache_dir, cache_path) != 0) {
            return 1;
        }

        bool cached = (access(cache_path, R_OK) == 0);
        if (cached) {
            if (!quiet) printf("cwal: found cached colorscheme.\n");
            if (cache_load_json(cache_path, &pal) != 0) {
                cached = false; /* fall through to regenerate */
            }
        }

        if (!cached) {
            if (!quiet) printf("cwal: generating a colorscheme...\n");

            image_t img;
            if (image_load(image, &img) != 0) {
                return 1;
            }

            hex_t raw[16];
            if (quantize_kmeans(img.pixels, img.count, raw, CWAL_KMEANS_K) != 0) {
                fprintf(stderr, "cwal: quantization failed.\n");
                image_free(&img);
                return 1;
            }
            image_free(&img);

            /* Sort darkest-first so palette_adjust picks a sensible bg. */
            sort_colors_by_yiq(raw, CWAL_KMEANS_K);

            /* Apply pywal-style adjustment for dark/light theme. */
            palette_adjust(raw, light);

            /* Optional extra saturation. */
            if (sat && *sat) {
                double amt = atof(sat);
                palette_saturate(raw, amt);
            }

            palette_build((const hex_t *)raw, image, alpha, &pal);

            if (cache_save_json(&pal, cache_path) != 0) {
                /* non-fatal */
            }
            if (!quiet) printf("cwal: generation complete.\n");
        }
    }

    /* Override background if requested. */
    if (bg_override) {
        char buf[8];
        const char *p = bg_override;
        if (*p != '#') {
            snprintf(buf, sizeof(buf), "#%s", p);
            p = buf;
        }
        snprintf(pal.background, sizeof(hex_t), "%s", p);
        memcpy(pal.colors[0], pal.background, sizeof(hex_t));
    }

    /* Send to terminals. */
    if (!skip_term) {
        char *blob = sequences_build(&pal, vte_fix);
        if (blob) {
            int n = sequences_send_to_ttys(blob);
            char seq_path[PATH_MAX];
            snprintf(seq_path, sizeof(seq_path),
                     "%s/sequences", cache_dir);
            cache_save_sequences(blob, seq_path);
            if (!quiet) {
                printf("cwal: sent to %d terminal(s).\n", n);
            }
            free(blob);
        }
    }

    /* Always refresh colors.json (so -R can restore later). */
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/colors.json", cache_dir);
        cache_save_json(&pal, path);
    }

    /* Template export: walk every template file (built-in + user)
     * and write the substituted result to the cache dir. */
    if (!skip_export) {
        char tpl_dir[PATH_MAX];
        char user_dir[PATH_MAX];
        char user_tpl[PATH_MAX];
        resolve_template_dir(tpl_dir, sizeof(tpl_dir));
        resolve_conf_dir(user_dir, sizeof(user_dir));
        snprintf(user_tpl, sizeof(user_tpl), "%s/templates", user_dir);

        int n = template_every(&pal, cache_dir, tpl_dir, user_tpl);
        if (!quiet) {
            printf("cwal: exported %d template(s) to %s.\n", n, cache_dir);
        }
    }

    /* Print the palette preview if attached to a tty. */
    if (isatty(STDOUT_FILENO) && !quiet) {
        palette_print(&pal);
    }

    return 0;
}
