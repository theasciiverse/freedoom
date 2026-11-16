/*
 * Asciiverse: the pictures for doom-ascii (-chars half), for terminals with
 * 24-bit colour, chosen in Options, Picture (av_picture in the config):
 *
 *   0 half blocks  two pixels a cell, one above the other: the upper half
 *                  block (U+2580) in the upper pixel's colour on the lower's.
 *                  Cells are about twice as tall as wide, so they are square.
 *   1 sextants     2x3 sub-pixels a cell in two colours (U+1FB00 on).
 *   2 Braille      2x4 dots a cell in two colours (U+2800 on); the dots are
 *                  square and the second colour fills between them.
 *   3 to 5         chafa (fine, blocks, ASCII), when built with libchafa.
 *   6              libcaca, 16 colours, when built with it.
 *
 * The game's frame (320x200 with -scaling 1) is fitted to the terminal at
 * Doom's 4:3, centred, and follows the terminal when it is resized. Our own
 * pictures send only the cells that changed since the last frame.
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

#ifdef AV_HAVE_CHAFA
#include <chafa.h>
extern char **environ;
#endif
#ifdef AV_HAVE_CACA
#include <caca.h>
#endif

/* The Picture setting, saved in the config as av_picture. */
int av_picture = 0;

unsigned AV_Pictures(void)
{
    unsigned mask = 1u | 2u | 4u;

#ifdef AV_HAVE_CHAFA
    mask |= 8u | 16u | 32u;
#endif
#ifdef AV_HAVE_CACA
    mask |= 64u;
#endif
    return mask;
}

typedef struct
{
    uint32_t fg, bg;            /* 0xRRGGBB, or ~0 for not drawn yet */
    uint32_t ch;                /* 0 for half blocks (fg over bg) */
} cell_t;

static cell_t *cells;
static unsigned cell_cols, cell_rows;
static int drawn_picture = -1;
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

static char *PutAt(char *p, unsigned row, unsigned col)
{
    *p++ = '\033';
    *p++ = '[';
    p = PutNum(p, row + 1);
    *p++ = ';';
    p = PutNum(p, col + 1);
    *p++ = 'H';
    return p;
}

static char *PutUtf8(char *p, uint32_t c)
{
    if (c < 0x80)
        *p++ = (char) c;
    else if (c < 0x800)
    {
        *p++ = (char) (0xc0 | (c >> 6));
        *p++ = (char) (0x80 | (c & 0x3f));
    }
    else if (c < 0x10000)
    {
        *p++ = (char) (0xe0 | (c >> 12));
        *p++ = (char) (0x80 | ((c >> 6) & 0x3f));
        *p++ = (char) (0x80 | (c & 0x3f));
    }
    else
    {
        *p++ = (char) (0xf0 | (c >> 18));
        *p++ = (char) (0x80 | ((c >> 12) & 0x3f));
        *p++ = (char) (0x80 | ((c >> 6) & 0x3f));
        *p++ = (char) (0x80 | (c & 0x3f));
    }
    return p;
}

static unsigned Lum(uint32_t c)
{
    return ((c >> 16) & 255) * 3 + ((c >> 8) & 255) * 6 + (c & 255);
}

static uint32_t Mean(const uint32_t *c, int n, unsigned mask, unsigned want)
{
    unsigned r = 0, g = 0, b = 0, k = 0;
    int i;

    for (i = 0; i < n; i++)
    {
        if (((mask >> i) & 1u) != want)
            continue;
        r += (c[i] >> 16) & 255;
        g += (c[i] >> 8) & 255;
        b += c[i] & 255;
        k++;
    }
    if (!k)
        return 0;
    return ((r / k) << 16) | ((g / k) << 8) | (b / k);
}

/* Braille's dot bits for the 2x4 grid read row by row */
static const unsigned char braille_bit[8] = {
    0x01, 0x08, 0x02, 0x10, 0x04, 0x20, 0x40, 0x80
};

/* One cell of a w x h picture (h in half-block pixels): its colours and
   character, for half blocks (style 0), sextants (1) or Braille (2). */
static void Cell(cell_t *c, int style, const uint32_t *fb, unsigned fw,
                 unsigned fh, unsigned w, unsigned h, unsigned cx, unsigned cy)
{
    unsigned xa = cx * fw / w, xb = (cx + 1) * fw / w;
    uint32_t sub[8];
    unsigned l[8], avg = 0, mask = 0, full;
    int ny, n, i;

    c->ch = 0;
    if (style == 0)
    {
        unsigned py = cy * 2;

        c->fg = Average(fb, fw, fh, xa, xb, py * fh / h, (py + 1) * fh / h);
        c->bg = Average(fb, fw, fh, xa, xb, (py + 1) * fh / h, (py + 2) * fh / h);
        return;
    }
    /* 2 columns and ny rows of sub-pixels; the picture has h / 2 rows of
       cells, so ny * h / 2 rows of sub-pixels */
    ny = style == 2 ? 4 : 3;
    n = 2 * ny;
    full = (1u << n) - 1;
    for (i = 0; i < n; i++)
    {
        unsigned sx = (unsigned) i & 1, sy = (unsigned) i >> 1;
        unsigned subw = 2 * w, subh = (unsigned) ny * (h / 2);
        unsigned gx = cx * 2 + sx, gy = cy * (unsigned) ny + sy;

        sub[i] = Average(fb, fw, fh, gx * fw / subw, (gx + 1) * fw / subw,
                         gy * fh / subh, (gy + 1) * fh / subh);
        l[i] = Lum(sub[i]);
        avg += l[i];
    }
    avg /= (unsigned) n;
    for (i = 0; i < n; i++)
        if (l[i] > avg)
            mask |= 1u << i;
    if (mask == 0 || mask == full)
    {
        c->fg = c->bg = Mean(sub, n, 0, 0);     /* one colour: all of them */
        return;
    }
    c->fg = Mean(sub, n, mask, 1);
    c->bg = Mean(sub, n, mask, 0);
    if (style == 2)
    {
        unsigned dots = 0;

        for (i = 0; i < 8; i++)
            if (mask & (1u << i))
                dots |= braille_bit[i];
        c->ch = 0x2800 + dots;
    }
    else if (mask == 21)
        c->ch = 0x258c;         /* left half block */
    else if (mask == 42)
        c->ch = 0x2590;         /* right half block */
    else
        c->ch = 0x1fb00 + mask - 1 - (mask > 21) - (mask > 42);
}

/* ---- chafa and libcaca: the whole picture each frame ------------------ */

#ifdef AV_HAVE_CHAFA
static ChafaCanvas *chafa_canvas;
static ChafaTermInfo *chafa_term;
static unsigned chafa_w, chafa_h;
static int chafa_symbols = -1;
static uint8_t *rgb;
static size_t rgb_size;

static void ChafaFree(void)
{
    if (chafa_canvas)
        chafa_canvas_unref(chafa_canvas);
    chafa_canvas = NULL;
}

static bool ChafaSetup(unsigned w, unsigned h, int symbols)
{
    ChafaSymbolMap *map;
    ChafaCanvasConfig *cc;
    ChafaSymbolTags tags;

    if (chafa_canvas && w == chafa_w && h == chafa_h && symbols == chafa_symbols)
        return true;
    ChafaFree();
    if (chafa_term == NULL)
        chafa_term = chafa_term_db_detect(chafa_term_db_get_default(), environ);
    tags = symbols == 0 ? CHAFA_SYMBOL_TAG_ALL
         : symbols == 1 ? (CHAFA_SYMBOL_TAG_BLOCK | CHAFA_SYMBOL_TAG_SPACE)
         : (CHAFA_SYMBOL_TAG_ASCII);
    map = chafa_symbol_map_new();
    chafa_symbol_map_add_by_tags(map, tags);
    cc = chafa_canvas_config_new();
    chafa_canvas_config_set_geometry(cc, (gint) w, (gint) h);
    chafa_canvas_config_set_cell_geometry(cc, 10, 20);
    chafa_canvas_config_set_canvas_mode(cc, CHAFA_CANVAS_MODE_TRUECOLOR);
    chafa_canvas_config_set_pixel_mode(cc, CHAFA_PIXEL_MODE_SYMBOLS);
    chafa_canvas_config_set_symbol_map(cc, map);
    chafa_canvas_config_set_optimizations(cc, CHAFA_OPTIMIZATION_ALL);
    chafa_canvas_config_set_work_factor(cc, 0.15f);
    chafa_canvas = chafa_canvas_new(cc);
    chafa_canvas_config_unref(cc);
    chafa_symbol_map_unref(map);
    chafa_w = w;
    chafa_h = h;
    chafa_symbols = symbols;
    return chafa_canvas != NULL;
}
#endif

#ifdef AV_HAVE_CACA
static caca_canvas_t *caca_cv;
static caca_dither_t *caca_di;
static unsigned caca_fw, caca_fh;
#endif

/* Text with newlines, each line placed at (row0 + i, col0). */
static void PutLines(const char *s, size_t len, unsigned row0, unsigned col0)
{
    size_t need = len + 32 * (len / 8 + 64);
    char *p;
    unsigned row = row0;
    size_t i = 0;

    if (out_size < need)
    {
        free(out);
        out_size = need;
        out = malloc(out_size);
    }
    p = out;
    while (i < len)
    {
        size_t j = i;

        while (j < len && s[j] != '\n')
            j++;
        p = PutAt(p, row++, col0);
        while (i < j)
        {
            if (s[i] != '\r')
                *p++ = s[i];
            i++;
        }
        i = j + 1;
    }
    memcpy(p, "\033[0m", 4);
    p += 4;
    fwrite(out, 1, (size_t) (p - out), stdout);
    fflush(stdout);
}

static void DrawWhole(int picture, const uint32_t *fb, unsigned fw,
                      unsigned fh, unsigned w, unsigned rows,
                      unsigned row0, unsigned x0)
{
#ifdef AV_HAVE_CHAFA
    if (picture >= 3 && picture <= 5)
    {
        GString *gs;
        size_t i, n = (size_t) fw * fh;

        if (!ChafaSetup(w, rows, picture - 3))
            return;
        if (rgb_size < n * 3)
        {
            free(rgb);
            rgb_size = n * 3;
            rgb = malloc(rgb_size);
        }
        for (i = 0; i < n; i++)
        {
            rgb[i * 3] = (uint8_t) (fb[i] >> 16);
            rgb[i * 3 + 1] = (uint8_t) (fb[i] >> 8);
            rgb[i * 3 + 2] = (uint8_t) fb[i];
        }
        chafa_canvas_draw_all_pixels(chafa_canvas, CHAFA_PIXEL_RGB8, rgb,
                                     (gint) fw, (gint) fh, (gint) fw * 3);
        gs = chafa_canvas_print(chafa_canvas, chafa_term);
        if (gs)
        {
            PutLines(gs->str, gs->len, row0, x0);
            g_string_free(gs, TRUE);
        }
        return;
    }
#endif
#ifdef AV_HAVE_CACA
    if (picture == 6)
    {
        size_t len = 0;
        void *buf;

        if (caca_cv == NULL)
            caca_cv = caca_create_canvas((int) w, (int) rows);
        else if ((unsigned) caca_get_canvas_width(caca_cv) != w ||
                 (unsigned) caca_get_canvas_height(caca_cv) != rows)
            caca_set_canvas_size(caca_cv, (int) w, (int) rows);
        if (caca_di == NULL || caca_fw != fw || caca_fh != fh)
        {
            if (caca_di)
                caca_free_dither(caca_di);
            caca_di = caca_create_dither(32, (int) fw, (int) fh, (int) fw * 4,
                                         0xff0000, 0xff00, 0xff, 0);
            caca_fw = fw;
            caca_fh = fh;
        }
        if (caca_cv == NULL || caca_di == NULL)
            return;
        caca_dither_bitmap(caca_cv, 0, 0, (int) w, (int) rows, caca_di, fb);
        buf = caca_export_canvas_to_memory(caca_cv, "utf8", &len);
        if (buf)
        {
            PutLines(buf, len, row0, x0);
            free(buf);
        }
        return;
    }
#endif
    (void) picture; (void) fb; (void) fw; (void) fh;
    (void) w; (void) rows; (void) row0; (void) x0;
}

void AV_DrawHalf(const uint32_t *fb, unsigned fw, unsigned fh)
{
    struct winsize ws;
    unsigned cols = 80, rows = 24, w, h, x0, row0, cx, cy;
    uint32_t fg = ~0u, bg = ~0u;
    bool fresh;
    int picture = av_picture;
    char *p;

    if (tolerance < 0)
    {
        const char *t = getenv("AV_TOLERANCE");
        tolerance = t != NULL ? atoi(t) : 6;
    }
    if (picture < 0 || picture > 6 || !(AV_Pictures() & (1u << picture)))
        picture = 0;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0)
    {
        cols = ws.ws_col;
        rows = ws.ws_row;
    }

    /* a new size or picture: forget what is on screen, start from clear */
    fresh = cols != cell_cols || rows != cell_rows || cells == NULL ||
            picture != drawn_picture;
    if (fresh)
    {
        free(cells);
        cells = malloc(sizeof(cell_t) * cols * rows);
        memset(cells, 0xff, sizeof(cell_t) * cols * rows);
        cell_cols = cols;
        cell_rows = rows;
        drawn_picture = picture;
        free(out);
        /* at most: a move, two colours and a character for every cell */
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

    if (fresh)
    {
        fwrite("\033[0m\033[2J", 1, 8, stdout);
        fflush(stdout);
    }
    if (picture >= 3)
    {
        DrawWhole(picture, fb, fw, fh, w, h / 2, row0, x0);
        return;
    }

    p = out;
    for (cy = 0; cy < h / 2; cy++)
    {
        bool at = false;    /* the cursor is where the next cell goes */

        for (cx = 0; cx < w; cx++)
        {
            cell_t c, *old = &cells[(row0 + cy) * cols + x0 + cx];

            Cell(&c, picture, fb, fw, fh, w, h, cx, cy);
            if (c.ch == old->ch && Near(c.fg, old->fg) && Near(c.bg, old->bg))
            {
                at = false;
                continue;
            }
            *old = c;
            if (!at)
            {
                p = PutAt(p, row0 + cy, x0 + cx);
                at = true;
            }
            if (c.fg == c.bg)
            {
                /* one colour: a space on it */
                if (bg != c.bg)
                {
                    p = PutColour(p, 1, c.bg);
                    bg = c.bg;
                }
                *p++ = ' ';
                continue;
            }
            if (fg != c.fg)
            {
                p = PutColour(p, 0, c.fg);
                fg = c.fg;
            }
            if (bg != c.bg)
            {
                p = PutColour(p, 1, c.bg);
                bg = c.bg;
            }
            p = PutUtf8(p, c.ch ? c.ch : 0x2580);   /* upper half block */
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
