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
 * This file is in the public domain.
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "doomkeys.h"

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
            else
            {
                return i + 1;           /* not a key: skip it */
            }
        }
        if (i >= len)
            return len > 32 ? len : 0;  /* incomplete (or junk) */

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
    fflush(stdout);
}

void AV_InputShutdown(void)
{
    fputs("\033[<u", stdout);
    fflush(stdout);
    if (have_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
}

void AV_ReadInput(uint32_t now)
{
    ssize_t n;
    size_t used = 0;
    int k;

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
