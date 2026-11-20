/*
 * cwal - Pywal clone in C
 * Copyright (c) 2026 - Ported from Python by Alecaishere/CuerdOS Dev. Team
 *
 *
 * Shared declarations for all modules.
 */
#ifndef CWAL_H
#define CWAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define CWAL_VERSION       "0.1.0"
#define CWAL_CACHE_VERSION "1.1.0"
#define CWAL_KMEANS_K      16
#define CWAL_KMEANS_ITERS  15
#define CWAL_SUBSAMPLE_MAX 10000  /* cap on pixels fed to k-means */

/* ---------- Color types ---------- */

typedef struct {
    uint8_t r, g, b;
} rgb_t;

typedef struct {
    double h, l, s;
} hls_t;

/* A hex color is stored as "#rrggbb\0" (8 bytes). */
typedef char hex_t[8];

/* ---------- Palette / colors dict ---------- */

typedef struct {
    char wallpaper[4096];
    char alpha[8];            /* "100" by default */
    hex_t background;         /* special.background */
    hex_t foreground;         /* special.foreground */
    hex_t cursor;             /* special.cursor */
    hex_t colors[16];         /* color0..color15 */
} palette_t;

/* ---------- color.c ---------- */

rgb_t   hex_to_rgb(const char *hex);
void    rgb_to_hex(rgb_t c, char *out /* 8 bytes */);
hex_t  *darken_color(const char *in, double amount, hex_t *out);
hex_t  *lighten_color(const char *in, double amount, hex_t *out);
hex_t  *blend_color(const char *a, const char *b, hex_t *out);
hex_t  *saturate_color(const char *in, double amount, hex_t *out);
double  rgb_to_yiq(rgb_t c);  /* for sorting */

/* ---------- image.c ---------- */

typedef struct {
    rgb_t *pixels;
    size_t count;
    int    w, h;
} image_t;

/* Load image from disk and return a subsampled array of rgb pixels.
 * Returns 0 on success, non-zero on error. */
int  image_load(const char *path, image_t *out);

void image_free(image_t *img);

/* ---------- quantize.c ---------- */

/* Run k-means on `pixels` to find K dominant colors.
 * Writes K hex colors into `out` (which must hold K hex_t slots).
 * Returns 0 on success. */
int  quantize_kmeans(const rgb_t *pixels, size_t n,
                     hex_t *out, int k);

/* Sort a list of hex colors by YIQ luminance (darkest first). */
void sort_colors_by_yiq(hex_t *colors, int n);

/* ---------- palette.c ---------- */

/* Adjust the raw 16 colors for a dark or light theme.
 * Mirrors pywal's backends/wal.py:adjust(). */
void palette_adjust(hex_t *colors /* in/out, 16 slots */, bool light);

/* Apply extra saturation (0.0..1.0). amount<=0 is a no-op. */
void palette_saturate(hex_t *colors, double amount);

/* Build a palette_t from 16 colors + wallpaper path + alpha. */
void palette_build(const hex_t *colors, const char *wallpaper,
                   const char *alpha, palette_t *pal);

/* Pretty-print the palette to stdout using ANSI background blocks. */
void palette_print(const palette_t *pal);

/* ---------- cache.c ---------- */

/* Compute the cache filename for an image+backend+light+sat.
 * Writes a path into `out` (must be at least PATH_MAX).
 * Returns 0 on success. */
int  cache_fname(const char *img, const char *backend, bool light,
                 const char *sat, const char *cache_dir, char *out);

/* Ensure a directory exists (mkdir -p). Returns 0 on success. */
int  ensure_dir(const char *path);

/* Save a palette_t as colors.json at the given path. */
int  cache_save_json(const palette_t *pal, const char *path);

/* Load a colors.json into a palette_t. */
int  cache_load_json(const char *path, palette_t *pal);

/* Save the raw escape-sequence blob (so users can `cat` it later). */
int  cache_save_sequences(const char *blob, const char *path);

/* ---------- sequences.c ---------- */

/* Build the full escape-sequence blob for a palette. Caller frees. */
char *sequences_build(const palette_t *pal, bool vte_fix);

/* Send a blob to every matching tty under /dev/pts on Linux/BSD or
 * /dev/ttys00* on macOS. Returns number of ttys written. */
int  sequences_send_to_ttys(const char *blob);

/* ---------- templates.c ---------- */

/* Substitute {key}, {key.attr}, {key.method(args)} placeholders in
 * `input` using the palette. Returns a malloc'd string. */
char *template_substitute(const char *input, const palette_t *pal);

/* Process one template file: read, substitute, write to out_path. */
int  template_process_file(const char *in_path, const char *out_path,
                           const palette_t *pal);

/* Walk every file in builtin_dir AND user_dir, run it through the
 * template engine, and write the result to cache_dir with the same
 * filename. Returns number of files processed. */
int  template_every(const palette_t *pal, const char *cache_dir,
                    const char *builtin_dir, const char *user_dir);

#endif /* CWAL_H */
