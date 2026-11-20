/*
 * cwal - Pywal clone in C
 * Copyright (c) 2026 - Ported from Python by Alecaishere/CuerdOS Dev. Team
 *
 * quantize.c - K-means color quantization.
 *
 * Replaces pywal's default backend (which shells out to ImageMagick
 * for -colors N -unique-colors). We do straight Lloyd's algorithm
 * with k-means++ initialization, then sort centroids by YIQ so the
 * palette is darkest-first (which is what the rest of the pipeline
 * expects: colors[0] is treated as the background).
 *
 * Complexity: O(iters * n * k). With n capped at ~10k and k=16 and
 * iters=15 that's about 2.4M distance computations per image, well
 * under 50ms on a modern CPU.
 */
#include "cwal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static double dist2(rgb_t a, rgb_t b)
{
    double dr = (double)a.r - (double)b.r;
    double dg = (double)a.g - (double)b.g;
    double db = (double)a.b - (double)b.b;
    return dr*dr + dg*dg + db*db;
}

/* xorshift128 PRNG: deterministic enough for color sampling,
 * no libc rand() skew across platforms. */
static uint32_t rng_state[4];

static void rng_seed(uint32_t s)
{
    if (s == 0) s = 0xdeadbeefu;
    for (int i = 0; i < 4; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        rng_state[i] = s;
    }
}

static uint32_t rng_next(void)
{
    uint32_t t = rng_state[3];
    uint32_t s = rng_state[0];
    rng_state[3] = rng_state[2];
    rng_state[2] = rng_state[1];
    rng_state[1] = s;
    t ^= s;
    rng_state[0] = t ^ s ^ (t >> 15) ^ (s << 21);
    return rng_state[0];
}

static size_t rng_below(size_t bound)
{
    if (bound == 0) return 0;
    return rng_next() % (uint32_t)bound;
}

/* k-means++ initialization: pick the first centroid at random,
 * then each subsequent one weighted by squared distance to the
 * nearest already-picked centroid. */
static void kmeanspp(const rgb_t *pixels, size_t n,
                     rgb_t *centroids, int k)
{
    centroids[0] = pixels[rng_below(n)];

    double *min_d = malloc(n * sizeof(double));
    if (!min_d) return;

    for (size_t i = 0; i < n; i++)
        min_d[i] = dist2(pixels[i], centroids[0]);

    for (int c = 1; c < k; c++) {
        double sum = 0.0;
        for (size_t i = 0; i < n; i++) sum += min_d[i];

        size_t pick = 0;
        if (sum <= 0.0) {
            pick = rng_below(n);
        } else {
            double r = (double)(rng_next() % 1000000) / 1000000.0 * sum;
            double acc = 0.0;
            for (size_t i = 0; i < n; i++) {
                acc += min_d[i];
                if (acc >= r) { pick = i; break; }
            }
        }
        centroids[c] = pixels[pick];

        for (size_t i = 0; i < n; i++) {
            double d = dist2(pixels[i], centroids[c]);
            if (d < min_d[i]) min_d[i] = d;
        }
    }

    free(min_d);
}

int quantize_kmeans(const rgb_t *pixels, size_t n,
                    hex_t *out, int k)
{
    if (!pixels || n == 0 || k <= 0) return 1;

    rng_seed((uint32_t)time(NULL));

    /* Cap k to the number of distinct pixels we actually have. */
    if ((size_t)k > n) k = (int)n;

    rgb_t *centroids = malloc((size_t)k * sizeof(rgb_t));
    int   *counts    = malloc((size_t)k * sizeof(int));
    size_t *labels   = malloc(n * sizeof(size_t));
    if (!centroids || !counts || !labels) {
        free(centroids); free(counts); free(labels);
        return 1;
    }

    kmeanspp(pixels, n, centroids, k);

    for (int iter = 0; iter < CWAL_KMEANS_ITERS; iter++) {
        /* assign step */
        bool changed = false;
        for (size_t i = 0; i < n; i++) {
            double best = -1.0;
            size_t bi = 0;
            for (int c = 0; c < k; c++) {
                double d = dist2(pixels[i], centroids[c]);
                if (best < 0.0 || d < best) { best = d; bi = (size_t)c; }
            }
            if (labels[i] != bi) {
                labels[i] = bi;
                changed = true;
            }
        }

        if (!changed && iter > 0) break; /* converged */

        /* update step */
        double *sr = calloc((size_t)k, sizeof(double));
        double *sg = calloc((size_t)k, sizeof(double));
        double *sb = calloc((size_t)k, sizeof(double));
        memset(counts, 0, (size_t)k * sizeof(int));
        if (!sr || !sg || !sb) {
            free(sr); free(sg); free(sb);
            break;
        }
        for (size_t i = 0; i < n; i++) {
            int c = (int)labels[i];
            sr[c] += pixels[i].r;
            sg[c] += pixels[i].g;
            sb[c] += pixels[i].b;
            counts[c]++;
        }
        for (int c = 0; c < k; c++) {
            if (counts[c] > 0) {
                centroids[c].r = (uint8_t)(sr[c] / counts[c] + 0.5);
                centroids[c].g = (uint8_t)(sg[c] / counts[c] + 0.5);
                centroids[c].b = (uint8_t)(sb[c] / counts[c] + 0.5);
            } else {
                /* empty cluster: re-seed from a random pixel */
                centroids[c] = pixels[rng_below(n)];
            }
        }
        free(sr); free(sg); free(sb);
    }

    /* emit hex */
    for (int c = 0; c < k; c++) {
        rgb_to_hex(centroids[c], out[c]);
    }

    free(centroids);
    free(counts);
    free(labels);
    return 0;
}

/* ---------- sorting ---------- */

void sort_colors_by_yiq(hex_t *colors, int n)
{
    /* simple insertion sort by YIQ luminance (ascending = darkest first) */
    for (int i = 1; i < n; i++) {
        hex_t cur;
        memcpy(cur, colors[i], sizeof(hex_t));
        double ycur = rgb_to_yiq(hex_to_rgb(cur));
        int j = i - 1;
        while (j >= 0 && rgb_to_yiq(hex_to_rgb(colors[j])) > ycur) {
            memcpy(colors[j + 1], colors[j], sizeof(hex_t));
            j--;
        }
        memcpy(colors[j + 1], cur, sizeof(hex_t));
    }
}
