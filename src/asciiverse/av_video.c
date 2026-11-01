/*
 * Asciiverse: the "half" picture for doom-ascii (-chars half), for
 * terminals with 24-bit colour.
 *
 * Each character cell shows two pixels, one above the other: the upper
 * half block (U+2580) in the upper pixel's colour on the lower pixel's
 * colour. Cells are about twice as tall as wide, so these pixels are
 * square. The game's frame (320x200 with -scaling 1) is fitted to the
 * terminal at Doom's 4:3, centred, each pixel the average of the frame's
 * pixels it covers; it follows the terminal when it is resized. Only the
 * cells that changed since the last frame are sent.
 *
 * This file is in the public domain.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

typedef struct
{
    uint32_t top, bottom;       /* 0xRRGGBB, or ~0 for not drawn yet */
} cell_t;

static cell_t *cells;
static unsigned cell_cols, cell_rows;
static char *out;
static size_t out_size;

/* the frame's pixels as 0x00RRGGBB (doomgeneric's are B, G, R, A bytes) */
static uint32_t Average(const uint32_t *fb, unsigned fw, unsigned fh,
                        unsigned x0, unsigned x1, unsigned y0, unsigned y1)
{
    uint32_t r = 0, g = 0, b = 0, n = 0;
    unsigned x, y;

    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    if (x1 > fw) x1 = fw;
    if (y1 > fh) y1 = fh;
    for (y = y0; y < y1; y++)
    {
        const uint32_t *p = fb + (size_t) y * fw + x0;

        for (x = x0; x < x1; x++, p++)
        {
            r += (*p >> 16) & 0xff;
            g += (*p >> 8) & 0xff;
            b += *p & 0xff;
            n++;
        }
    }
    if (n == 0)
        return 0;
    return ((r / n) << 16) | ((g / n) << 8) | (b / n);
}

/* close enough to what is on screen to leave it: within `tolerance` in
   each of red, green and blue (AV_TOLERANCE, default 6; 0 sends every
   change). Small flicker in averaged textures then costs nothing. */
static int tolerance = -1;

static bool Near(uint32_t a, uint32_t b)
{
    int d;

    if (a == b)
        return true;
    if (a == ~0u || b == ~0u)
        return false;
    d = (int) ((a >> 16) & 0xff) - (int) ((b >> 16) & 0xff);
    if (d > tolerance || -d > tolerance) return false;
    d = (int) ((a >> 8) & 0xff) - (int) ((b >> 8) & 0xff);
    if (d > tolerance || -d > tolerance) return false;
    d = (int) (a & 0xff) - (int) (b & 0xff);
    return d <= tolerance && -d <= tolerance;
}

static char *PutNum(char *p, unsigned v)
{
    char tmp[12];
    int n = 0;

    do
    {
        tmp[n++] = (char) ('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *p++ = tmp[--n];
    return p;
}

static char *PutColour(char *p, int ground, uint32_t c)
{
    *p++ = '\033';
    *p++ = '[';
    *p++ = ground == 0 ? '3' : '4';
    *p++ = '8';
    *p++ = ';';
    *p++ = '2';
    *p++ = ';';
    p = PutNum(p, (c >> 16) & 0xff);
    *p++ = ';';
    p = PutNum(p, (c >> 8) & 0xff);
    *p++ = ';';
    p = PutNum(p, c & 0xff);
    *p++ = 'm';
    return p;
}

void AV_DrawHalf(const uint32_t *fb, unsigned fw, unsigned fh)
{
    struct winsize ws;
    unsigned cols = 80, rows = 24, w, h, x0, row0, cx, cy;
    uint32_t fg = ~0u, bg = ~0u;
    bool resized;
    char *p;

    if (tolerance < 0)
    {
        const char *t = getenv("AV_TOLERANCE");
        tolerance = t != NULL ? atoi(t) : 6;
    }
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0)
    {
        cols = ws.ws_col;
        rows = ws.ws_row;
    }

    /* a new size: forget what is on screen and start from a clear one */
    resized = cols != cell_cols || rows != cell_rows || cells == NULL;
    if (resized)
    {
        free(cells);
        cells = malloc(sizeof(cell_t) * cols * rows);
        memset(cells, 0xff, sizeof(cell_t) * cols * rows);
        cell_cols = cols;
        cell_rows = rows;
        free(out);
        /* at most: a move, two colours and a block for every cell */
        out_size = (size_t) cols * rows * 64 + 64;
        out = malloc(out_size);
    }

    /* the picture at 4:3 in square pixels, as big as fits */
    if (cols * 3 / 4 <= rows * 2)
    {
        w = cols;
        h = cols * 3 / 4;
    }
    else
    {
        h = rows * 2;
        w = h * 4 / 3;
    }
    h &= ~1u;
    if (w == 0 || h == 0)
        return;
    x0 = (cols - w) / 2;
    row0 = (rows - h / 2) / 2;

    p = out;
    if (resized)
    {
        memcpy(p, "\033[0m\033[2J", 8);
        p += 8;
    }
    for (cy = 0; cy < h / 2; cy++)
    {
        bool at = false;    /* the cursor is where the next cell goes */
        unsigned py = cy * 2;
        unsigned ya = py * fh / h, yb = (py + 1) * fh / h, yc = (py + 2) * fh / h;

        for (cx = 0; cx < w; cx++)
        {
            unsigned xa = cx * fw / w, xb = (cx + 1) * fw / w;
            cell_t c, *old = &cells[(row0 + cy) * cols + x0 + cx];

            c.top = Average(fb, fw, fh, xa, xb, ya, yb);
            c.bottom = Average(fb, fw, fh, xa, xb, yb, yc);
            if (Near(c.top, old->top) && Near(c.bottom, old->bottom))
            {
                at = false;
                continue;
            }
            *old = c;
            if (!at)
            {
                *p++ = '\033';
                *p++ = '[';
                p = PutNum(p, row0 + cy + 1);
                *p++ = ';';
                p = PutNum(p, x0 + cx + 1);
                *p++ = 'H';
                at = true;
            }
            if (c.top == c.bottom)
            {
                /* one colour: a space on it */
                if (bg != c.bottom)
                {
                    p = PutColour(p, 1, c.bottom);
                    bg = c.bottom;
                }
                *p++ = ' ';
            }
            else
            {
                if (fg != c.top)
                {
                    p = PutColour(p, 0, c.top);
                    fg = c.top;
                }
                if (bg != c.bottom)
                {
                    p = PutColour(p, 1, c.bottom);
                    bg = c.bottom;
                }
                memcpy(p, "\xe2\x96\x80", 3);   /* U+2580 upper half block */
                p += 3;
            }
        }
    }
    if (p != out)
    {
        memcpy(p, "\033[0m", 4);
        p += 4;
        fwrite(out, 1, (size_t) (p - out), stdout);
        fflush(stdout);
    }
}
