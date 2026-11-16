/*
 * Asciiverse: keyboard input for doom-ascii on Unix terminals.
 *
 * Doom needs to know when a key goes down and when it comes up; a
 * terminal normally reports only presses (and, while a key is held, its
 * repeats). Two ways in:
 *
 * - Terminals that speak the kitty keyboard protocol (kitty, Ghostty, foot,
 *   WezTerm, Alacritty, iTerm2 and others) are asked to report every key,
 *   with its press, repeat and release (CSI > 11 u, undone at exit): Doom
 *   gets the exact presses and releases, and Shift, Ctrl and Alt become
 *   keys of their own (Doom's run, fire and strafe).
 * - Elsewhere a release is inferred when a key's repeats stop: a key
 *   counts as held for 550 ms after a fresh press (the terminal's usual
 *   delay before it starts repeating), then for 100 ms after each repeat.
 *
 * All waiting input is read each frame and an escape sequence split
 * between two reads is kept for the next, so no release goes missing.
 *
 * The mouse (off with -nomouse): the terminal is asked to report every
 * movement and button (any-event tracking, CSI ? 1003 h, in SGR form,
 * CSI ? 1006 h), and for positions in pixels rather than cells (CSI ? 1016
 * h) where it can; a DECRQM query says whether it took. A terminal reports
 * where the pointer is, not how far the mouse moved, so turning comes from
 * the change in position: moving the pointer across the whole window turns
 * once round at the default sensitivity. Buttons are Doom's (left fires,
 * right strafes, middle walks forward, a double click of either uses), the
 * wheel changes weapon. Up and down are not used, as with DOS's novert.
 *
 * This file is in the public domain.
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "doomkeys.h"
#include "m_argv.h"

#define PRE_REPEAT_MS 550
#define REPEAT_GAP_MS 100
#define QUEUE_LEN 256

typedef struct
{
    bool down;
    bool exact;             /* last seen through the kitty protocol */
    unsigned repeats;
    uint32_t last_ms;
} held_t;

typedef struct
{
    bool pressed;
    unsigned char key;
} av_event_t;

static held_t held[256];
static av_event_t queue[QUEUE_LEN];
static unsigned q_head, q_tail;
static unsigned char pending[256];
static size_t pending_len;
static uint32_t pending_since;
static struct termios saved;
static bool have_saved;

/* the mouse */
static bool mouse_on;           /* tracking asked for */
static bool mouse_pixels;       /* positions in pixels (1016), not cells */
static bool mouse_log;          /* stderr is a file: log every report */
static int mouse_buttons;       /* Doom's bits: 1 left, 2 right, 4 middle */
static bool mouse_changed;
static int last_x = -1;
static double turn_per_unit;    /* Doom mouse units per cell or pixel */
static double mouse_acc;        /* movement not yet passed on */
static unsigned wheel_up, wheel_down;
static int clicked;             /* buttons pressed since last passed on */
static int wheel_step;          /* 1: a wheel press was sent, release next */
static bool tic_done;           /* a click went to Doom: the rest next tic */

static void Push(unsigned char key, bool pressed)
{
    unsigned next = (q_tail + 1) % QUEUE_LEN;

    if (key == 0 || next == q_head)
        return;
    queue[q_tail].key = key;
    queue[q_tail].pressed = pressed;
    q_tail = next;
}

/* a key seen: kind 1 press, 2 repeat, 3 release; 0 a plain terminal's
   press or repeat (no release will come) */
static void Seen(unsigned char key, int kind, uint32_t now)
{
    held_t *h = &held[key];

    if (key == 0)
        return;
    if (kind == 3)
    {
        if (h->down)
            Push(key, false);
        h->down = false;
        return;
    }
    h->exact = kind != 0;
    if (!h->down)
    {
        h->down = true;
        h->repeats = 0;
        Push(key, true);
    }
    else
    {
        h->repeats++;
    }
    h->last_ms = now;
}

/* the kitty protocol's key numbers (and plain characters) as Doom keys */
static unsigned char FromCodepoint(unsigned code)
{
    switch (code)
    {
        case 13: case 10: return KEY_ENTER;
        case 27: return KEY_ESCAPE;
        case 9: return KEY_TAB;
        case 127: case 8: return KEY_BACKSPACE;
        case 57441: case 57447: return KEY_RSHIFT;    /* left, right Shift */
        case 57442: case 57448: return KEY_RCTRL;     /* left, right Ctrl */
        case 57443: case 57449: return KEY_RALT;      /* left, right Alt */
        case 57399: return KEYP_0;
        case 57400: return KEYP_1;
        case 57401: return KEYP_2;
        case 57402: return KEYP_3;
        case 57403: return KEYP_4;
        case 57404: return KEYP_5;
        case 57405: return KEYP_6;
        case 57406: return KEYP_7;
        case 57407: return KEYP_8;
        case 57408: return KEYP_9;
        case 57414: return KEY_ENTER;                 /* keypad Enter */
        case 57417: return KEY_LEFTARROW;             /* keypad arrows */
        case 57418: return KEY_RIGHTARROW;
        case 57419: return KEY_UPARROW;
        case 57420: return KEY_DOWNARROW;
        default:
            if (code >= 32 && code < 127)
                return (unsigned char) tolower((int) code);
            return 0;
    }
}

/* CSI <number> ~ */
static unsigned char FromTilde(unsigned n)
{
    switch (n)
    {
        case 1: case 7: return KEY_HOME;
        case 2: return KEY_INS;
        case 3: return KEY_DEL;
        case 4: case 8: return KEY_END;
        case 5: return KEY_PGUP;
        case 6: return KEY_PGDN;
        case 11: return KEY_F1;
        case 12: return KEY_F2;
        case 13: return KEY_F3;
        case 14: return KEY_F4;
        case 15: return KEY_F5;
        case 17: return KEY_F6;
        case 18: return KEY_F7;
        case 19: return KEY_F8;
        case 20: return KEY_F9;
        case 21: return KEY_F10;
        case 23: return KEY_F11;
        case 24: return KEY_F12;
        default: return 0;
    }
}

/* CSI or SS3 with a letter at the end */
static unsigned char FromFinal(unsigned char c)
{
    switch (c)
    {
        case 'A': return KEY_UPARROW;
        case 'B': return KEY_DOWNARROW;
        case 'C': return KEY_RIGHTARROW;
        case 'D': return KEY_LEFTARROW;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        case 'P': return KEY_F1;
        case 'Q': return KEY_F2;
        case 'R': return KEY_F3;
        case 'S': return KEY_F4;
        default: return 0;
    }
}

/* Doom's mouse units for one turn round at the default sensitivity
   (angleturn -= units * 8, and 65536 is a full turn) */
#define UNITS_PER_TURN 8192.0
/* a jump of more than this share of the window in one report is the
   pointer coming back in from somewhere else, not a movement */
#define MAX_JUMP 0.25

static void MouseScale(void)
{
    struct winsize ws;
    unsigned cols = 80, xpix = 0;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
    {
        if (ws.ws_col > 0)
            cols = ws.ws_col;
        xpix = ws.ws_xpixel;
    }
    if (mouse_pixels)
        turn_per_unit = UNITS_PER_TURN / (xpix > 0 ? xpix : cols * 10);
    else
        turn_per_unit = UNITS_PER_TURN / cols;
}

/* an SGR mouse report: button code b at column/pixel x, y */
static void Mouse(unsigned b, unsigned x, unsigned y, bool release)
{
    if (mouse_log)
        fprintf(stderr, "mouse: b=%u x=%u y=%u %s\n", b, x, y,
                release ? "release" : "press/move");

    if (b & 64)
    {
        /* the wheel: 64 up, 65 down (66, 67 sideways, unused) */
        if (!release && (b & 3) == 0)
            wheel_up++;
        else if (!release && (b & 3) == 1)
            wheel_down++;
        return;
    }

    if (last_x >= 0)
    {
        double d = ((double) x - last_x) * turn_per_unit;

        if (d < UNITS_PER_TURN * MAX_JUMP && d > -UNITS_PER_TURN * MAX_JUMP)
        {
            mouse_acc += d;
            if (d != 0)
                mouse_changed = true;
        }
    }
    last_x = (int) x;

    if (!(b & 32))
    {
        /* a button: 0 left, 1 middle, 2 right */
        static const int bit[4] = { 1, 4, 2, 0 };
        int m = bit[b & 3];

        if (release)
        {
            mouse_buttons &= ~m;
        }
        else
        {
            mouse_buttons |= m;
            clicked |= m;
        }
        mouse_changed = true;
    }
}

/* One key from the start of buf: its length, or 0 when the sequence is not
   complete yet. */
static size_t ParseOne(const unsigned char *buf, size_t len, uint32_t now,
                       bool final_read)
{
    if (buf[0] != 0x1b)
    {
        unsigned char c = buf[0];

        Seen(FromCodepoint(c == '\r' ? 13 : c), 0, now);
        return 1;
    }
    if (len < 2)
    {
        /* a lone Escape, unless the rest of a sequence is on its way */
        if (!final_read)
            return 0;
        Seen(KEY_ESCAPE, 0, now);
        return 1;
    }
    if (buf[1] == 'O')
    {
        if (len < 3)
            return 0;
        Seen(FromFinal(buf[2]), 0, now);
        return 3;
    }
    if (buf[1] == '[')
    {
        /* CSI params final: params are digits, ';' and ':' */
        unsigned field[4][3];
        int f = 0, sub = 0;
        size_t i = 2;
        int kind;
        unsigned char key;
        unsigned char inter = 0;

        if (mouse_on && len >= 3 && buf[2] == 'M')
        {
            /* an old-style mouse report, CSI M b x y, from a terminal
               without SGR reports */
            unsigned b = len > 3 ? (unsigned) buf[3] - 32 : 0;

            if (len < 6)
                return 0;
            if ((b & 3) == 3 && !(b & 96))
            {
                /* a release, which in this form does not say which
                   button: all of them */
                mouse_buttons = 0;
                mouse_changed = true;
                b = 32;                 /* then just the position */
            }
            if (buf[3] >= 32 && buf[4] > 32 && buf[5] > 32)
                Mouse(b, (unsigned) buf[4] - 32, (unsigned) buf[5] - 32, false);
            return 6;
        }

        memset(field, 0, sizeof field);
        if (i < len && (buf[i] == '?' || buf[i] == '>' || buf[i] == '<' || buf[i] == '='))
            i++;
        for (; i < len; i++)
        {
            unsigned char c = buf[i];

            if (c >= '0' && c <= '9')
            {
                if (f < 4 && sub < 3)
                    field[f][sub] = field[f][sub] * 10 + (c - '0');
            }
            else if (c == ';')
            {
                f++;
                sub = 0;
            }
            else if (c == ':')
            {
                sub++;
            }
            else if (c >= 0x40 && c <= 0x7e)
            {
                break;
            }
            else if (c >= 0x20 && c <= 0x2f)
            {
                inter = c;              /* an intermediate, as in $ y */
            }
            else
            {
                return i + 1;           /* not a key: skip it */
            }
        }
        if (i >= len)
            return len > 32 ? len : 0;  /* incomplete (or junk) */

        if (buf[2] == '<' && (buf[i] == 'M' || buf[i] == 'm'))
        {
            /* an SGR mouse report: CSI < b ; x ; y M (m on release) */
            if (mouse_on)
                Mouse(field[0][0], field[1][0], field[2][0], buf[i] == 'm');
            return i + 1;
        }
        if (buf[2] == '?' && inter == '$' && buf[i] == 'y')
        {
            /* DECRQM's answer: CSI ? mode ; state $ y, state 1 set */
            if (field[0][0] == 1016 && field[1][0] == 1)
            {
                mouse_pixels = true;
                last_x = -1;
                MouseScale();
            }
            if (mouse_log && field[0][0] == 1016)
                fprintf(stderr, "mouse: positions in %s\n",
                        mouse_pixels ? "pixels" : "cells");
            return i + 1;
        }

        /* the event type is the modifier field's sub-field: 1 press,
           2 repeat, 3 release; missing means a plain press */
        kind = field[1][1];
        switch (buf[i])
        {
            case 'u':
                key = FromCodepoint(field[0][0]);
                break;
            case '~':
                key = FromTilde(field[0][0]);
                break;
            default:
                key = FromFinal(buf[i]);
                break;
        }
        if (buf[2] == '?')
            key = 0;                    /* a reply to a query, not a key */
        Seen(key, kind, now);
        return i + 1;
    }
    /* Escape then another key: Alt+key in a plain terminal; drop the
       Escape and take the key */
    return 1;
}

void AV_InputInit(void)
{
    struct termios t;

    if (tcgetattr(STDIN_FILENO, &saved) == 0)
    {
        have_saved = true;
        t = saved;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
    /* kitty keyboard protocol: disambiguate (1), report event types (2)
       and report all keys as escape codes (8); terminals without it
       ignore this */
    fputs("\033[>11u", stdout);
    /* the mouse: every movement (1003), SGR reports (1006), in pixels
       where the terminal can (1016), and ask whether it can */
    mouse_on = M_CheckParm("-nomouse") == 0;
    mouse_log = !isatty(STDERR_FILENO);
    if (mouse_on)
    {
        fputs("\033[?1003h\033[?1006h\033[?1016h\033[?1016$p", stdout);
        MouseScale();
    }
    fflush(stdout);
}

void AV_InputShutdown(void)
{
    fputs("\033[<u", stdout);
    if (mouse_on)
        fputs("\033[?1016l\033[?1006l\033[?1003l", stdout);
    fflush(stdout);
    if (have_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
}

void AV_ReadInput(uint32_t now)
{
    ssize_t n;
    size_t used = 0;
    int k;

    if (mouse_on)
        MouseScale();               /* the window may have changed size */
    tic_done = false;

    /* everything waiting, kept with what was left over last time */
    for (;;)
    {
        if (pending_len == sizeof pending)
            break;
        n = read(STDIN_FILENO, pending + pending_len, sizeof pending - pending_len);
        if (n <= 0)
            break;
        if (pending_len == 0)
            pending_since = now;
        pending_len += (size_t) n;
    }

    while (used < pending_len)
    {
        /* a sequence still incomplete after 50 ms is taken as it is (a
           lone Escape) */
        bool final_read = now - pending_since > 50;
        size_t len = ParseOne(pending + used, pending_len - used, now, final_read);

        if (len == 0)
            break;
        used += len;
    }
    if (used > 0)
    {
        memmove(pending, pending + used, pending_len - used);
        pending_len -= used;
        pending_since = now;
    }

    /* plain terminals: a key is let go when its repeats stop */
    for (k = 1; k < 256; k++)
    {
        held_t *h = &held[k];
        uint32_t window;

        if (!h->down || h->exact)
            continue;
        window = h->repeats == 0 ? PRE_REPEAT_MS : REPEAT_GAP_MS;
        if (now - h->last_ms > window)
        {
            h->down = false;
            Push((unsigned char) k, false);
        }
    }
}

int AV_GetKey(int *pressed, unsigned char *key)
{
    if (q_head == q_tail)
        return 0;
    *pressed = queue[q_head].pressed;
    *key = queue[q_head].key;
    q_head = (q_head + 1) % QUEUE_LEN;
    return 1;
}

/* The mouse as Doom events, called after the keys until it returns 0:
   a wheel click (a press, then its release), then the buttons and the
   movement since the last call. *dx is in Doom's mouse units. A click
   whose release has already come is passed on held for one tic, its
   release in the next, and one wheel click goes a tic, so Doom sees
   every one. */
int AV_GetMouse(int *buttons, int *dx)
{
    int whole;

    if (!mouse_on || tic_done)
        return 0;
    if (wheel_step)
    {
        wheel_step = 0;
        tic_done = true;
        *buttons = mouse_buttons;
        *dx = 0;
        return 1;
    }
    if (wheel_up || wheel_down)
    {
        /* Doom's mouse buttons 3 and 4 are the wheel, up and down */
        int bit = wheel_up ? 8 : 16;

        if (wheel_up)
            wheel_up--;
        else
            wheel_down--;
        wheel_step = 1;
        if (mouse_log)
            fprintf(stderr, "mouse: to Doom wheel %s\n", bit == 8 ? "up" : "down");
        *buttons = mouse_buttons | bit;
        *dx = 0;
        return 1;
    }
    if (!mouse_changed)
        return 0;
    whole = (int) mouse_acc;
    mouse_acc -= whole;
    *buttons = mouse_buttons | clicked;
    if (clicked & ~mouse_buttons)
        tic_done = true;            /* released already: that comes next tic */
    else
        mouse_changed = false;
    clicked = 0;
    *dx = whole;
    if (mouse_log)
        fprintf(stderr, "mouse: to Doom buttons=%d dx=%d\n", *buttons, *dx);
    return 1;
}
