/* A virtual Xbox-style gamepad through /dev/uinput, for testing the game's
 * gamepad (src/asciiverse/av_pad.c) without one. Build: cc -o test-pad
 * asciiverse/test-pad.c; run it with a FIFO on stdin and the game with
 * 2>pad.log. This file is in the public domain.
 *
 * Reads commands from stdin, one per line:
 *   b <code> <0|1>     button (hex or decimal EV_KEY code)
 *   a <code> <value>   axis (ABS code)
 *   s <ms>             sleep
 *   q                  quit (removes the device)
 */
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int fd;

static void emit(int type, int code, int val)
{
    struct input_event ie;
    memset(&ie, 0, sizeof ie);
    ie.type = type;
    ie.code = code;
    ie.value = val;
    if (write(fd, &ie, sizeof ie) != sizeof ie)
        perror("uinput write");
}

static void absax(int code, int min, int max)
{
    struct uinput_abs_setup a;
    memset(&a, 0, sizeof a);
    a.code = code;
    a.absinfo.minimum = min;
    a.absinfo.maximum = max;
    a.absinfo.value = min < 0 ? 0 : min;
    ioctl(fd, UI_SET_ABSBIT, code);
    ioctl(fd, UI_ABS_SETUP, &a);
}

int main(void)
{
    static const int btns[] = { BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR,
        BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR };
    struct uinput_setup us;
    char line[128];

    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("uinput"); return 1; }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    for (unsigned i = 0; i < sizeof btns / sizeof *btns; i++)
        ioctl(fd, UI_SET_KEYBIT, btns[i]);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    absax(ABS_X, -32768, 32767); absax(ABS_Y, -32768, 32767);
    absax(ABS_RX, -32768, 32767); absax(ABS_RY, -32768, 32767);
    absax(ABS_Z, 0, 1023); absax(ABS_RZ, 0, 1023);
    absax(ABS_HAT0X, -1, 1); absax(ABS_HAT0Y, -1, 1);
    memset(&us, 0, sizeof us);
    us.id.bustype = BUS_USB;
    us.id.vendor = 0x045e;   /* looks like an Xbox 360 pad */
    us.id.product = 0x028e;
    strcpy(us.name, "Asciiverse test pad");
    ioctl(fd, UI_DEV_SETUP, &us);
    ioctl(fd, UI_DEV_CREATE);
    fprintf(stderr, "vpad ready\n");
    while (fgets(line, sizeof line, stdin)) {
        char c; int a = 0, b = 0;
        if (sscanf(line, " %c %i %i", &c, &a, &b) < 1) continue;
        if (c == 'b') { emit(EV_KEY, a, b); emit(EV_SYN, SYN_REPORT, 0); }
        else if (c == 'a') { emit(EV_ABS, a, b); emit(EV_SYN, SYN_REPORT, 0); }
        else if (c == 's') { struct timespec t = { a / 1000, (a % 1000) * 1000000L }; nanosleep(&t, 0); }
        else if (c == 'q') break;
    }
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
