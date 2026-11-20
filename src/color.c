/*
 * cwal - Pywal clone in C
 * Copyright (c) 2026 - Ported from Python by Alecaishere/CuerdOS Dev. Team
 *
 * color.c - Color manipulation utilities.
 *
 * Pure C port of pywal/util.py color functions:
 *   hex_to_rgb, rgb_to_hex, darken_color, lighten_color,
 *   blend_color, saturate_color, rgb_to_yiq.
 *
 * HLS conversion is implemented inline because libc has no colorsys.
 */
#include "cwal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------- hex <-> rgb ---------- */

rgb_t hex_to_rgb(const char *hex)
{
    rgb_t c = {0, 0, 0};
    if (!hex) return c;
    /* skip leading '#' */
    const char *p = (hex[0] == '#') ? hex + 1 : hex;
    /* accept #rgb or #rrggbb */
    if (strlen(p) == 6) {
        char buf[3] = {0};
        buf[0] = p[0]; buf[1] = p[1];
        c.r = (uint8_t)strtoul(buf, NULL, 16);
        buf[0] = p[2]; buf[1] = p[3];
        c.g = (uint8_t)strtoul(buf, NULL, 16);
        buf[0] = p[4]; buf[1] = p[5];
        c.b = (uint8_t)strtoul(buf, NULL, 16);
    } else if (strlen(p) == 3) {
        char buf[3];
        buf[0] = buf[1] = p[0];
        c.r = (uint8_t)strtoul(buf, NULL, 16);
        buf[0] = buf[1] = p[1];
        c.g = (uint8_t)strtoul(buf, NULL, 16);
        buf[0] = buf[1] = p[2];
        c.b = (uint8_t)strtoul(buf, NULL, 16);
    }
    return c;
}

void rgb_to_hex(rgb_t c, char *out /* 8 bytes */)
{
    snprintf(out, 8, "#%02x%02x%02x", c.r, c.g, c.b);
}

/* ---------- darken / lighten / blend ---------- */

hex_t *darken_color(const char *in, double amount, hex_t *out)
{
    rgb_t c = hex_to_rgb(in);
    rgb_t r;
    r.r = (uint8_t)(c.r * (1.0 - amount));
    r.g = (uint8_t)(c.g * (1.0 - amount));
    r.b = (uint8_t)(c.b * (1.0 - amount));
    rgb_to_hex(r, *out);
    return out;
}

hex_t *lighten_color(const char *in, double amount, hex_t *out)
{
    rgb_t c = hex_to_rgb(in);
    rgb_t r;
    r.r = (uint8_t)(c.r + (255 - c.r) * amount);
    r.g = (uint8_t)(c.g + (255 - c.g) * amount);
    r.b = (uint8_t)(c.b + (255 - c.b) * amount);
    rgb_to_hex(r, *out);
    return out;
}

hex_t *blend_color(const char *a, const char *b, hex_t *out)
{
    rgb_t x = hex_to_rgb(a);
    rgb_t y = hex_to_rgb(b);
    rgb_t r;
    r.r = (uint8_t)(0.5 * x.r + 0.5 * y.r);
    r.g = (uint8_t)(0.5 * x.g + 0.5 * y.g);
    r.b = (uint8_t)(0.5 * x.b + 0.5 * y.b);
    rgb_to_hex(r, *out);
    return out;
}

/* ---------- HLS conversion (mirrors Python colorsys) ---------- */

static double min3(double a, double b, double c)
{
    double m = a;
    if (b < m) m = b;
    if (c < m) m = c;
    return m;
}

static double max3(double a, double b, double c)
{
    double m = a;
    if (b > m) m = b;
    if (c > m) m = c;
    return m;
}

/* Python colorsys.rgb_to_hls(r,g,b) -- inputs in [0,1] */
static hls_t rgb_to_hls(double r, double g, double b)
{
    hls_t hls;
    double mx = max3(r, g, b);
    double mn = min3(r, g, b);
    hls.l = (mx + mn) / 2.0;

    if (mx == mn) {
        /* achromatic */
        hls.h = 0.0;
        hls.s = 0.0;
        return hls;
    }

    double d = mx - mn;
    hls.s = (hls.l > 0.5) ? (d / (2.0 - mx - mn)) : (d / (mx + mn));

    if      (mx == r) hls.h = (g - b) / d + (g < b ? 6.0 : 0.0);
    else if (mx == g) hls.h = (b - r) / d + 2.0;
    else              hls.h = (r - g) / d + 4.0;
    hls.h /= 6.0;
    return hls;
}

/* Python colorsys.hls_to_rgb(h,l,s) -- outputs in [0,1] */
static rgb_t hls_to_rgb(double h, double l, double s)
{
    rgb_t out;
    double r, g, b;

    if (s == 0.0) {
        r = g = b = l; /* achromatic */
    } else {
        double q = (l < 0.5) ? (l * (1.0 + s)) : (l + s - l * s);
        double p = 2.0 * l - q;
        double hk;
        double tr, tg, tb;

        hk = h + 1.0/3.0; if (hk < 0) hk += 1.0; if (hk > 1) hk -= 1.0;
        if (hk < 1.0/6.0) tr = p + (q - p) * 6.0 * hk;
        else if (hk < 0.5) tr = q;
        else if (hk < 2.0/3.0) tr = p + (q - p) * (2.0/3.0 - hk) * 6.0;
        else tr = p;

        tg = h; if (tg < 0) tg += 1.0; if (tg > 1) tg -= 1.0;
        if (tg < 1.0/6.0) g = p + (q - p) * 6.0 * tg;
        else if (tg < 0.5) g = q;
        else if (tg < 2.0/3.0) g = p + (q - p) * (2.0/3.0 - tg) * 6.0;
        else g = p;

        tb = h - 1.0/3.0; if (tb < 0) tb += 1.0; if (tb > 1) tb -= 1.0;
        if (tb < 1.0/6.0) b = p + (q - p) * 6.0 * tb;
        else if (tb < 0.5) b = q;
        else if (tb < 2.0/3.0) b = p + (q - p) * (2.0/3.0 - tb) * 6.0;
        else b = p;

        r = tr; g = g; b = b;
    }

    out.r = (uint8_t)(r * 255.0 + 0.5);
    out.g = (uint8_t)(g * 255.0 + 0.5);
    out.b = (uint8_t)(b * 255.0 + 0.5);
    return out;
}

hex_t *saturate_color(const char *in, double amount, hex_t *out)
{
    rgb_t c = hex_to_rgb(in);
    double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    hls_t hls = rgb_to_hls(r, g, b);
    hls.s = amount;  /* pywal replaces saturation outright */
    rgb_t n = hls_to_rgb(hls.h, hls.l, hls.s);
    rgb_to_hex(n, *out);
    return out;
}

/* ---------- YIQ luminance (used for sorting) ---------- */

double rgb_to_yiq(rgb_t c)
{
    /* Python colorsys.rgb_to_yiq -- the I and Q don't matter for sort */
    double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    return 0.30 * r + 0.59 * g + 0.11 * b;
}
