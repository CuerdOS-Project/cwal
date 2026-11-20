/*
 * cwal - Pywal clone in C
 * Copyright (c) 2026 - Ported from Python by Alecaishere/CuerdOS Dev. Team
 *
 * image.c - Image loading via stb_image + subsampling.
 *
 * Mirrors pywal's approach: ImageMagick uses -resize 25% before
 * quantization. We do a similar subsample (cap at CWAL_SUBSAMPLE_MAX
 * pixels by striding), which keeps k-means fast on big images.
 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_PSD
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

#include "cwal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int image_load(const char *path, image_t *out)
{
    int w, h, ch;
    unsigned char *data = stbi_load(path, &w, &h, &ch, 3);
    if (!data) {
        fprintf(stderr, "cwal: failed to load '%s': %s\n",
                path, stbi_failure_reason());
        return 1;
    }

    /* Pick a stride so we feed at most CWAL_SUBSAMPLE_MAX pixels to
     * k-means. Equivalent to ImageMagick's -resize 25% for small
     * images, more aggressive for huge ones. */
    size_t total = (size_t)w * (size_t)h;
    size_t stride = 1;
    if (total > CWAL_SUBSAMPLE_MAX) {
        stride = total / CWAL_SUBSAMPLE_MAX;
        if (stride < 1) stride = 1;
    }

    size_t cap = total / stride + 1;
    rgb_t *pixels = malloc(cap * sizeof(rgb_t));
    if (!pixels) {
        stbi_image_free(data);
        return 1;
    }

    size_t n = 0;
    for (size_t i = 0; i < total; i += stride) {
        unsigned char *px = data + i * 3;
        pixels[n].r = px[0];
        pixels[n].g = px[1];
        pixels[n].b = px[2];
        n++;
    }

    stbi_image_free(data);

    out->pixels = pixels;
    out->count  = n;
    out->w      = w;
    out->h      = h;
    return 0;
}

void image_free(image_t *img)
{
    if (img && img->pixels) {
        free(img->pixels);
        img->pixels = NULL;
        img->count = 0;
    }
}
