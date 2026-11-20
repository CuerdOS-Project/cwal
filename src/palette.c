/*
 * cwal - Pywal clone in C
 * Copyright (c) 2026 - Ported from Python by Alecaishere/CuerdOS Dev. Team
 *
 * palette.c - Palette adjustment + helpers.
 *
 * Mirrors pywal/backends/wal.py:adjust() and pywal/colors.py
 * (saturate_colors, colors_to_dict, palette()).
 */
#include "cwal.h"

#include <stdio.h>
#include <string.h>

/* pywal wal.py:adjust():
 *   raw_colors = colors[:1] + colors[8:16] + colors[8:-1]
 *
 * With 16 input colors this becomes:
 *   raw = [c0] + [c8..c15] + [c8..c14]
 * i.e. 1 + 8 + 7 = 16 entries.
 *
 * Then for dark theme:
 *   if raw[0] is not pure black, darken by 0.40
 *   raw[7] = blend(raw[7], #EEEEEE)
 *   raw[8] = darken(raw[7], 0.30)
 *   raw[15] = blend(raw[15], #EEEEEE)
 *
 * For light theme:
 *   for each color: saturate(0.5)
 *   raw[0] = lighten(colors[-1], 0.85)
 *   raw[7] = colors[0]
 *   raw[8] = darken(colors[-1], 0.40)
 *   raw[15] = colors[0]
 */
void palette_adjust(hex_t *colors /* 16 in/out */, bool light)
{
    /* Build the rearranged 16-entry palette from a temp copy. */
    hex_t src[16];
    memcpy(src, colors, sizeof(src));

    hex_t raw[16];
    /* raw[0] = colors[0] */
    memcpy(raw[0], src[0], sizeof(hex_t));
    /* raw[1..8] = colors[8..15] */
    for (int i = 0; i < 8; i++)
        memcpy(raw[1 + i], src[8 + i], sizeof(hex_t));
    /* raw[9..15] = colors[8..14] */
    for (int i = 0; i < 7; i++)
        memcpy(raw[9 + i], src[8 + i], sizeof(hex_t));

    if (light) {
        for (int i = 0; i < 16; i++) {
            hex_t tmp;
            saturate_color(raw[i], 0.5, &tmp);
            memcpy(raw[i], tmp, sizeof(hex_t));
        }
        /* raw[0] = lighten(colors[-1], 0.85)
         * colors[-1] in a 16-list is colors[15]. */
        lighten_color(src[15], 0.85, &raw[0]);
        /* raw[7] = colors[0] */
        memcpy(raw[7], src[0], sizeof(hex_t));
        /* raw[8] = darken(colors[-1], 0.40) */
        darken_color(src[15], 0.40, &raw[8]);
        /* raw[15] = colors[0] */
        memcpy(raw[15], src[0], sizeof(hex_t));
    } else {
        /* darken background if not pure black */
        if (raw[0][1] != '0' || raw[0][2] != '0') {
            /* check if hex starts with "#00" -> second and third chars */
        }
        /* pywal checks `raw_colors[0][1] != "0"`, meaning the red
         * channel byte does NOT start with '0' (i.e. R >= 0x10). We
         * mirror that exactly. */
        if (raw[0][1] != '0') {
            hex_t tmp;
            darken_color(raw[0], 0.40, &tmp);
            memcpy(raw[0], tmp, sizeof(hex_t));
        }
        /* raw[7] = blend(raw[7], #EEEEEE) */
        hex_t b7;
        blend_color(raw[7], "#EEEEEE", &b7);
        memcpy(raw[7], b7, sizeof(hex_t));
        /* raw[8] = darken(raw[7], 0.30) */
        hex_t b8;
        darken_color(raw[7], 0.30, &b8);
        memcpy(raw[8], b8, sizeof(hex_t));
        /* raw[15] = blend(raw[15], #EEEEEE) */
        hex_t b15;
        blend_color(raw[15], "#EEEEEE", &b15);
        memcpy(raw[15], b15, sizeof(hex_t));
    }

    memcpy(colors, raw, sizeof(raw));
}

/* pywal colors.py:saturate_colors():
 *   if amount and amount <= 1.0:
 *       for i in 0..15:
 *           if i not in [0, 7, 8, 15]:
 *               colors[i] = saturate(colors[i], amount)
 */
void palette_saturate(hex_t *colors, double amount)
{
    if (amount <= 0.0 || amount > 1.0) return;
    for (int i = 0; i < 16; i++) {
        if (i == 0 || i == 7 || i == 8 || i == 15) continue;
        hex_t tmp;
        saturate_color(colors[i], amount, &tmp);
        memcpy(colors[i], tmp, sizeof(hex_t));
    }
}

void palette_build(const hex_t *colors, const char *wallpaper,
                   const char *alpha, palette_t *pal)
{
    memset(pal, 0, sizeof(*pal));
    snprintf(pal->wallpaper, sizeof(pal->wallpaper), "%s", wallpaper);
    snprintf(pal->alpha, sizeof(pal->alpha), "%s", alpha ? alpha : "100");

    memcpy(pal->background, colors[0], sizeof(hex_t));
    memcpy(pal->foreground, colors[15], sizeof(hex_t));
    memcpy(pal->cursor,     colors[15], sizeof(hex_t));

    for (int i = 0; i < 16; i++)
        memcpy(pal->colors[i], colors[i], sizeof(hex_t));
}

/* pywal colors.py:palette(): print 16 swatches in two rows of 8,
 * using the 256-color background escape \033[48;5;Nm. */
void palette_print(const palette_t *pal)
{
    printf("Current colorscheme:\n");
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 8; col++) {
            int idx = row * 8 + col;
            printf("\033[48;5;%dm%-6s\033[0m", 232 + idx, "");
            (void)pal;
        }
        printf("\n");
    }
    /* also print true-color swatches so the actual palette is visible */
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 8; col++) {
            int idx = row * 8 + col;
            rgb_t c = hex_to_rgb(pal->colors[idx]);
            printf("\033[48;2;%d;%d;%dm      \033[0m", c.r, c.g, c.b);
        }
        printf("\n");
    }
}
