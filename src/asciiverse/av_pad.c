/*
 * Asciiverse: gamepads, read on Linux straight from the kernel's event
 * devices (/dev/input/event*), with no library. A terminal cannot report a
 * gamepad, so the game opens one itself; desktops let the person at the
 * machine open them (systemd's uaccess rule for joysticks).
 *
 * In play, the pad drives the engine's own joystick input (ev_joystick),
 * with the sticks analog (g_game.c reads them through AV_PadTurn); while a
 * menu is open it sends the menu's keys instead, so prompts that wait for
 * "y" or "n" work from the pad too.
 *
 *   left stick   move and strafe        A        use; in menus, choose (yes)
 *   right stick  turn                   B        in menus, back (no)
 *   d-pad        move and turn; menus   LB / RB  previous / next weapon
 *   RT           fire                   Start    menu
 *   LT           run                    Back     automap
 *
 * Only real gamepads are used: devices with gamepad or joystick buttons
 * (0x120 to 0x13f) and no digitizer buttons (0x140 to 0x14f). Graphics
 * tablet pads and 3D mice are joystick devices too, and their resting axes
 * would turn the player without end. A pad plugged in while the game runs
 * is found within a second or two.
 *
 * With stderr redirected (doom-ascii 2>pad.log) every pad button, device
 * and menu change is logged there.
 *
 * Elsewhere than Linux there is no pad yet: AV_PadInit and AV_PadUpdate do
 * nothing (also on Linux when built without its kernel headers).
 *
 * This file is in the public domain.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"   /* the engine's bool, which is not C99's */
#include "d_event.h"
#include "doomkeys.h"
#include "m_controls.h"

void AV_PadInit(void);
void AV_PadUpdate(int now);
int AV_PadTurn(int x, int speed);
bool AV_PadMenuInput(int now);

/* full deflection of a stick, as the joystick event carries it */
#define FULL 32767

/* the turn at full deflection, in angle units a tic: the keyboard's fast
   turn (246 degrees a second), a little more while running */
#define TURN_MAX 1280
#define TURN_RUN 1600

/* the angle turned for a stick's x (-FULL to FULL): squared, so a small
   push aims finely and a full one turns quickly */
int AV_PadTurn(int x, int speed)
{
    double m = (double) x / FULL;
    double t = m * fabs(m) * (speed ? TURN_RUN : TURN_MAX);
    return (int) lround(t);
}

/* Linux, where its kernel headers are installed (a bare musl toolchain has
   none, and builds without the pad) */
#if defined(__linux__) && defined(__has_include)
#if __has_include(<linux/input.h>)
#define AV_PAD_EVDEV
#endif
#endif

#ifdef AV_PAD_EVDEV

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

extern bool menuactive;
extern int messageToPrint;
extern bool messageNeedsInput;

/* the pad's buttons, whatever the device calls them */
enum {
    P_A, P_B, P_X, P_Y, P_LB, P_RB, P_LT, P_RT, P_BACK, P_START, P_L3, P_R3,
    P_UP, P_DOWN, P_LEFT, P_RIGHT, P_COUNT
};
static const char *const pad_names[P_COUNT] = {
    "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Back", "Start", "L3", "R3",
    "Up", "Down", "Left", "Right"
};

/* the bits of the joystick event's buttons in play; AV_PadInit binds the
   engine's joyb_ settings to them */
enum { J_FIRE, J_USE, J_SPEED, J_PREV, J_NEXT };

enum { A_LX, A_LY, A_RX, A_RY, A_LT, A_RT, A_HX, A_HY, A_COUNT };

struct axis {
    int code;      /* ABS_ code on this device, or -1 */
    int min, max;
    int raw;
};

static int fd = -1;
static char dev_name[128];
static struct axis axes[A_COUNT];
static bool held[0x300];           /* EV_KEY codes held on the device */
static int next_scan;              /* when to look for a pad again */
static bool logging;

static bool pressed[P_COUNT];      /* this update */
static bool was_pressed[P_COUNT];  /* the last one */
static int sent_key[P_COUNT];      /* key sent for a button, to release it */
static int repeat_at[P_COUNT];     /* menu direction repeat */
static bool was_menu;
static int menu_choice_at = -1000; /* when the pad last chose in a menu */
static bool joy_zeroed = true;

#define LOG(...) do { if (logging) fprintf(stderr, "pad: " __VA_ARGS__); } while (0)

#define BITS_LONG (8 * sizeof(long))
#define TEST_BIT(bits, n) (((bits)[(n) / BITS_LONG] >> ((n) % BITS_LONG)) & 1)

static bool is_gamepad(int f, bool *has_rx)
{
    unsigned long keys[KEY_MAX / BITS_LONG + 1], abs_[ABS_MAX / BITS_LONG + 1];
    bool pad = false, digitizer = false;
    int i;

    memset(keys, 0, sizeof keys);
    memset(abs_, 0, sizeof abs_);
    if (ioctl(f, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0)
        return false;
    ioctl(f, EVIOCGBIT(EV_ABS, sizeof abs_), abs_);
    for (i = 0x120; i <= 0x13f; i++)
        pad |= TEST_BIT(keys, i);
    for (i = 0x140; i <= 0x14f; i++)
        digitizer |= TEST_BIT(keys, i);
    *has_rx = TEST_BIT(abs_, ABS_RX);
    return pad && !digitizer;
}

static void set_axis(int a, int code)
{
    struct input_absinfo info;

    axes[a].code = -1;
    if (ioctl(fd, EVIOCGABS(code), &info) < 0 || info.maximum <= info.minimum)
        return;
    axes[a].code = code;
    axes[a].min = info.minimum;
    axes[a].max = info.maximum;
    axes[a].raw = info.value;
}

static void open_pad(int now)
{
    DIR *dir;
    struct dirent *e;

    next_scan = now + 1500;
    dir = opendir("/dev/input");
    if (!dir)
        return;
    while ((e = readdir(dir)) != NULL) {
        char path[300];
        bool has_rx;
        int f;

        if (strncmp(e->d_name, "event", 5) != 0)
            continue;
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        f = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (f < 0)
            continue;
        if (!is_gamepad(f, &has_rx)) {
            close(f);
            continue;
        }
        fd = f;
        if (ioctl(fd, EVIOCGNAME(sizeof dev_name), dev_name) < 0)
            snprintf(dev_name, sizeof dev_name, "%s", path);
        set_axis(A_LX, ABS_X);
        set_axis(A_LY, ABS_Y);
        if (has_rx) {
            /* the standard layout (Xbox, PlayStation, Steam Deck): the
               right stick on RX/RY, analog triggers on Z/RZ */
            set_axis(A_RX, ABS_RX);
            set_axis(A_RY, ABS_RY);
            set_axis(A_LT, ABS_Z);
            set_axis(A_RT, ABS_RZ);
        } else {
            /* generic HID pads put the right stick on Z/RZ */
            set_axis(A_RX, ABS_Z);
            set_axis(A_RY, ABS_RZ);
            axes[A_LT].code = axes[A_RT].code = -1;
        }
        set_axis(A_HX, ABS_HAT0X);
        set_axis(A_HY, ABS_HAT0Y);
        memset(held, 0, sizeof held);
        LOG("using %s (%s)\n", dev_name, path);
        break;
    }
    closedir(dir);
}

static void close_pad(void)
{
    LOG("lost %s\n", dev_name);
    close(fd);
    fd = -1;
    memset(held, 0, sizeof held);
    for (int a = 0; a < A_COUNT; a++)
        axes[a].code = -1;
}

/* a stick axis, -FULL to FULL around its centre */
static int centred(int a)
{
    const struct axis *x = &axes[a];
    double half, v;

    if (x->code < 0)
        return 0;
    half = (x->max - x->min) / 2.0;
    v = (x->raw - (x->min + half)) / half;
    if (v > 1) v = 1;
    if (v < -1) v = -1;
    return (int) lround(v * FULL);
}

/* a trigger axis, 0 to FULL */
static int pulled(int a)
{
    const struct axis *x = &axes[a];

    if (x->code < 0)
        return 0;
    return (int) ((long) (x->raw - x->min) * FULL / (x->max - x->min));
}

/* a stick past its dead zone, rescaled so movement starts at zero */
#define DEAD 0.18

static void dead_zone_2d(int *x, int *y)
{
    double fx = (double) *x / FULL, fy = (double) *y / FULL;
    double m = sqrt(fx * fx + fy * fy), s;

    if (m < DEAD) {
        *x = *y = 0;
        return;
    }
    s = (m > 1 ? 1 : (m - DEAD) / (1 - DEAD)) / m;
    *x = (int) lround(fx * s * FULL);
    *y = (int) lround(fy * s * FULL);
}

static int dead_zone_1d(int v)
{
    double f = (double) v / FULL, m = fabs(f);

    if (m < DEAD)
        return 0;
    m = m > 1 ? 1 : (m - DEAD) / (1 - DEAD);
    return (int) lround((f < 0 ? -m : m) * FULL);
}

static void read_pad(void)
{
    struct input_event ev[64];
    ssize_t n;

    for (;;) {
        n = read(fd, ev, sizeof ev);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN)
                close_pad();
            return;
        }
        if (n == 0) {
            close_pad();
            return;
        }
        for (size_t i = 0; i < (size_t) n / sizeof ev[0]; i++) {
            if (ev[i].type == EV_KEY && ev[i].code < sizeof held)
                held[ev[i].code] = ev[i].value != 0;
            else if (ev[i].type == EV_ABS)
                for (int a = 0; a < A_COUNT; a++)
                    if (axes[a].code == ev[i].code)
                        axes[a].raw = ev[i].value;
        }
    }
}

/* the device's buttons as the pad's */
static void map_buttons(int lx, int ly)
{
    static const int standard[] = {
        BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_TL2,
        BTN_TR2, BTN_SELECT, BTN_START, BTN_THUMBL, BTN_THUMBR
    };
    int hx = centred(A_HX), hy = centred(A_HY);

    for (int p = 0; p <= P_R3; p++)
        /* a generic joystick's buttons, 0x120 up, in the same order */
        pressed[p] = held[standard[p]] || held[0x120 + p];
    pressed[P_LT] |= pulled(A_LT) > FULL / 3;
    pressed[P_RT] |= pulled(A_RT) > FULL / 3;
    pressed[P_UP] = held[BTN_DPAD_UP] || hy < 0;
    pressed[P_DOWN] = held[BTN_DPAD_DOWN] || hy > 0;
    pressed[P_LEFT] = held[BTN_DPAD_LEFT] || hx < 0;
    pressed[P_RIGHT] = held[BTN_DPAD_RIGHT] || hx > 0;
    /* in menus the left stick is a d-pad as well */
    if (menuactive) {
        pressed[P_UP] |= ly < -FULL / 2;
        pressed[P_DOWN] |= ly > FULL / 2;
        pressed[P_LEFT] |= lx < -FULL / 2;
        pressed[P_RIGHT] |= lx > FULL / 2;
    }
}

static void post_key(evtype_t type, int key)
{
    event_t ev;

    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.data1 = key;
    if (type == ev_keydown && key >= ' ' && key < 127)
        ev.data2 = key;
    D_PostEvent(&ev);
}

static void send_down(int p, int key)
{
    if (sent_key[p])
        post_key(ev_keyup, sent_key[p]);
    sent_key[p] = key;
    post_key(ev_keydown, key);
}

static void send_up(int p)
{
    if (sent_key[p]) {
        post_key(ev_keyup, sent_key[p]);
        sent_key[p] = 0;
    }
}

static void post_joystick(int buttons, int turn, int forward, int strafe)
{
    event_t ev;

    if (!buttons && !turn && !forward && !strafe) {
        if (joy_zeroed)
            return;
        joy_zeroed = true;
    } else {
        joy_zeroed = false;
    }
    memset(&ev, 0, sizeof ev);
    ev.type = ev_joystick;
    ev.data1 = buttons;
    ev.data2 = turn;
    ev.data3 = forward;
    ev.data4 = strafe;
    D_PostEvent(&ev);
}

static void menu_input(int now)
{
    static const int dirs[] = { P_UP, P_DOWN, P_LEFT, P_RIGHT };
    bool prompt = messageToPrint && messageNeedsInput;

    for (int i = 0; i < 4; i++) {
        int p = dirs[i];
        int key = p == P_UP ? key_menu_up : p == P_DOWN ? key_menu_down
                : p == P_LEFT ? key_menu_left : key_menu_right;

        if (pressed[p] && !was_pressed[p]) {
            send_down(p, key);
            repeat_at[p] = now + 400;
        } else if (pressed[p] && now >= repeat_at[p]) {
            send_down(p, key);
            repeat_at[p] = now + 110;
        }
    }
    if (pressed[P_A] && !was_pressed[P_A]) {
        send_down(P_A, prompt ? key_menu_confirm : key_menu_forward);
        menu_choice_at = now;
    }
    if (pressed[P_B] && !was_pressed[P_B])
        send_down(P_B, prompt ? key_menu_abort : key_menu_back);
    if (pressed[P_START] && !was_pressed[P_START])
        send_down(P_START, key_menu_activate);
}

static void play_input(void)
{
    int lx = centred(A_LX), ly = centred(A_LY), rx = centred(A_RX);
    int buttons = 0, turn, forward;

    dead_zone_2d(&lx, &ly);
    rx = dead_zone_1d(rx);
    if (pressed[P_START] && !was_pressed[P_START])
        send_down(P_START, key_menu_activate);
    if (pressed[P_BACK] && !was_pressed[P_BACK])
        send_down(P_BACK, key_map_toggle);

    if (pressed[P_RT]) buttons |= 1 << J_FIRE;
    if (pressed[P_A]) buttons |= 1 << J_USE;
    if (pressed[P_LT]) buttons |= 1 << J_SPEED;
    if (pressed[P_LB]) buttons |= 1 << J_PREV;
    if (pressed[P_RB]) buttons |= 1 << J_NEXT;

    /* the d-pad as the arrow keys: up and down move, left and right turn */
    turn = pressed[P_LEFT] ? -FULL : pressed[P_RIGHT] ? FULL : rx;
    forward = pressed[P_UP] ? -FULL : pressed[P_DOWN] ? FULL : ly;
    post_joystick(buttons, turn, forward, lx);
}

static void release_all(void)
{
    for (int p = 0; p < P_COUNT; p++) {
        send_up(p);
        pressed[p] = was_pressed[p] = false;
    }
    post_joystick(0, 0, 0, 0);
}

void AV_PadInit(void)
{
    static bool done;

    if (done)
        return;
    done = true;
    logging = !isatty(STDERR_FILENO);
    for (int a = 0; a < A_COUNT; a++)
        axes[a].code = -1;
    /* the engine's joystick buttons, as this file sends them; the joyb_
       settings in a config named SDL's buttons, which this build has not */
    joybfire = J_FIRE;
    joybuse = J_USE;
    joybspeed = J_SPEED;
    joybprevweapon = J_PREV;
    joybnextweapon = J_NEXT;
    joybstrafe = joybstrafeleft = joybstraferight = -1;
    joybjump = joybmenu = -1;
    open_pad(0);
}

void AV_PadUpdate(int now)
{
    int lx, ly;

    if (fd < 0) {
        if (now < next_scan)
            return;
        open_pad(now);
        if (fd < 0)
            return;
    }
    read_pad();
    if (fd < 0) {
        release_all();
        return;
    }

    lx = centred(A_LX);
    ly = centred(A_LY);
    map_buttons(lx, ly);
    for (int p = 0; p < P_COUNT; p++)
        if (pressed[p] != was_pressed[p])
            LOG("%s %s%s\n", pad_names[p], pressed[p] ? "down" : "up",
                menuactive ? " (menu)" : "");

    if (menuactive != was_menu) {
        LOG("%s\n", menuactive ? "menu open" : "menu closed");
        if (menuactive)
            post_joystick(0, 0, 0, 0); /* stop walking under the menu */
        was_menu = menuactive;
    }
    if (menuactive)
        menu_input(now);
    else
        play_input();

    for (int p = 0; p < P_COUNT; p++) {
        if (!pressed[p])
            send_up(p);
        was_pressed[p] = pressed[p];
    }
}

/* whether the menu choice being handled now came from the pad, which cannot
   type a saved game's name */
bool AV_PadMenuInput(int now)
{
    return now - menu_choice_at < 250;
}

#else /* not Linux, or no kernel headers: no pad */

void AV_PadInit(void)
{
}

void AV_PadUpdate(int now)
{
    (void) now;
}

bool AV_PadMenuInput(int now)
{
    (void) now;
    return false;
}

#endif
