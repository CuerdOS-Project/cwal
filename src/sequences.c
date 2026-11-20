/*
 * cwal - Pywal clone in C
 * Copyright (c) 2026 - Ported from Python by Alecaishere/CuerdOS Dev. Team
 *
 * sequences.c - Terminal escape sequence generation + delivery.
 *
 * Mirrors pywal/sequences.py. On Linux/BSD we write to every
 * /dev/pts/[0-9]*; on macOS to /dev/ttys00*.
 *
 * The escape formats used are:
 *   color 0..15:   \033]4;N;#rrggbb\033\\
 *   special 10/11/12/13/17/19: \033]N;#rrggbb\033\\
 *   232, 256, 257: color escapes (256-color slots used as aliases)
 *   708:           \033]708;#rrggbb\033\\ (background border, skipped
 *                  when --vte is set because VTE mangles it)
 *
 * On macOS, pywal uses \033]P%1x%s\033\\ (iTerm OSC). We auto-detect
 * the platform with uname() and switch formats accordingly.
 */
#include "cwal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <glob.h>
#include <fcntl.h>

static bool on_macos(void)
{
    struct utsname u;
    if (uname(&u) != 0) return false;
    return strstr(u.sysname, "Darwin") != NULL;
}

/* Append an OSC4 color escape for index/color into a growable buffer. */
static void append_color(char **buf, size_t *len, size_t *cap,
                         int index, const char *color)
{
    /* strip leading # */
    const char *hex = (color[0] == '#') ? color + 1 : color;
    char chunk[32];
    if (on_macos() && index < 20) {
        /* iTerm-style: \033]P{index:x}{hex}\033\\ */
        int n = snprintf(chunk, sizeof(chunk),
                         "\033]P%01x%s\033\\", index, hex);
        if (n > 0) {
            if (*len + (size_t)n + 1 > *cap) {
                *cap = (*len + (size_t)n + 1) * 2;
                *buf = realloc(*buf, *cap);
            }
            memcpy(*buf + *len, chunk, (size_t)n);
            *len += (size_t)n;
        }
        return;
    }
    int n = snprintf(chunk, sizeof(chunk),
                     "\033]4;%d;#%s\033\\", index, hex);
    if (n > 0) {
        if (*len + (size_t)n + 1 > *cap) {
            *cap = (*len + (size_t)n + 1) * 2;
            *buf = realloc(*buf, *cap);
        }
        memcpy(*buf + *len, chunk, (size_t)n);
        *len += (size_t)n;
    }
}

static void append_special(char **buf, size_t *len, size_t *cap,
                           int index, const char *color,
                           const char *alpha, bool vte_fix)
{
    const char *hex = (color[0] == '#') ? color + 1 : color;
    char chunk[64];
    int n;
    if (index == 11 && alpha && strcmp(alpha, "100") != 0) {
        /* \033]11;[alpha]#hex\033\\ */
        n = snprintf(chunk, sizeof(chunk),
                     "\033]%d;[%s]#%s\033\\", index, alpha, hex);
    } else if (index == 708 && vte_fix) {
        return; /* skip */
    } else {
        n = snprintf(chunk, sizeof(chunk),
                     "\033]%d;#%s\033\\", index, hex);
    }
    if (n > 0) {
        if (*len + (size_t)n + 1 > *cap) {
            *cap = (*len + (size_t)n + 1) * 2;
            *buf = realloc(*buf, *cap);
        }
        memcpy(*buf + *len, chunk, (size_t)n);
        *len += (size_t)n;
    }
}

char *sequences_build(const palette_t *pal, bool vte_fix)
{
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    /* colors 0..15 */
    for (int i = 0; i < 16; i++) {
        append_color(&buf, &len, &cap, i, pal->colors[i]);
    }

    /* specials */
    append_special(&buf, &len, &cap, 10, pal->foreground, NULL, false);
    append_special(&buf, &len, &cap, 11, pal->background, pal->alpha, false);
    append_special(&buf, &len, &cap, 12, pal->cursor,     NULL, false);
    append_special(&buf, &len, &cap, 13, pal->foreground, NULL, false);
    append_special(&buf, &len, &cap, 17, pal->foreground, NULL, false);
    append_special(&buf, &len, &cap, 19, pal->background, NULL, false);

    /* 256-color slot aliases */
    append_color(&buf, &len, &cap, 232, pal->background);
    append_color(&buf, &len, &cap, 256, pal->foreground);
    append_color(&buf, &len, &cap, 257, pal->background);

    if (!vte_fix) {
        append_special(&buf, &len, &cap, 708, pal->background, pal->alpha, false);
    }

    buf[len] = '\0';
    return buf;
}

int sequences_send_to_ttys(const char *blob)
{
    const char *pattern = on_macos()
        ? "/dev/ttys00[0-9]*"
        : "/dev/pts/[0-9]*";

    glob_t g;
    if (glob(pattern, 0, NULL, &g) != 0) {
        return 0;
    }

    int written = 0;
    size_t bloblen = strlen(blob);
    for (size_t i = 0; i < g.gl_pathc; i++) {
        int fd = open(g.gl_pathv[i], O_WRONLY | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) continue;
        /* writev-style best effort */
        ssize_t n = write(fd, blob, bloblen);
        (void)n;
        close(fd);
        written++;
    }

    globfree(&g);
    return written;
}
