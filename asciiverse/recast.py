#!/usr/bin/env python3
"""Re-encodes an asciicast v3 at a capped frame rate, for the catalog
preview: doom-ascii redraws at 35 frames a second, which makes a cast of
a minute well over 100 MB (hub/RECORDING.md, "File size"). The cast is
played through pyte; every 1/fps seconds the screen is compared with the
last frame written and only the cells that changed are written, a colour
counting as unchanged within `tolerance` in each of red, green and blue.
The last output (the terminal reset at exit) and the exit event are kept.

    python3 asciiverse/recast.py in.cast out.cast [fps] [tolerance]

Defaults: 15 fps, tolerance 8. Needs pyte.
"""
import json, sys

import pyte


class Screen(pyte.Screen):
    """pyte with REP (CSI n b), which ncurses-style output may use."""
    last = " "

    def draw(self, data):
        if data:
            self.last = data[-1]
        super().draw(data)

    def rep(self, n=1, *args, **kwargs):
        self.draw(self.last * max(n, 1))


class Stream(pyte.ByteStream):
    csi = dict(pyte.ByteStream.csi, b="rep")


def rgb(c):
    if isinstance(c, str) and len(c) == 6:
        try:
            return int(c, 16)
        except ValueError:
            return None
    return None


def near(a, b, tol):
    if a == b:
        return True
    ra, rb = rgb(a), rgb(b)
    if ra is None or rb is None:
        return False
    return all(abs(((ra >> s) & 255) - ((rb >> s) & 255)) <= tol for s in (16, 8, 0))


def sgr(fg, bg):
    parts = []
    r = rgb(fg)
    parts.append(f"38;2;{r >> 16};{(r >> 8) & 255};{r & 255}" if r is not None else "39")
    r = rgb(bg)
    parts.append(f"48;2;{r >> 16};{(r >> 8) & 255};{r & 255}" if r is not None else "49")
    return parts


def main():
    src, dst = sys.argv[1], sys.argv[2]
    fps = float(sys.argv[3]) if len(sys.argv) > 3 else 15.0
    tol = int(sys.argv[4]) if len(sys.argv) > 4 else 8
    lines = open(src).read().splitlines()
    header = json.loads(lines[0])
    cols, rows = header["term"]["cols"], header["term"]["rows"]
    events = [json.loads(l) for l in lines[1:]]
    screen = Screen(cols, rows)
    stream = Stream(screen)
    shown = [[None] * cols for _ in range(rows)]   # (char, fg, bg) written
    out = []
    t = last_t = 0.0
    step = 1.0 / fps
    next_frame = step
    outputs = [i for i, e in enumerate(events) if e[1] == "o"]
    last_output = outputs[-1] if outputs else -1

    def frame(at):
        nonlocal last_t
        buf = []
        cur_fg = cur_bg = None
        for y in range(rows):
            row = screen.buffer[y]
            x = 0
            moved = False
            while x < cols:
                ch = row[x]
                old = shown[y][x]
                same = old is not None and old[0] == ch.data and \
                    near(old[1], ch.fg, tol) and near(old[2], ch.bg, tol)
                if same:
                    moved = False
                    x += 1
                    continue
                if not moved:
                    buf.append(f"\x1b[{y + 1};{x + 1}H")
                    moved = True
                if (ch.fg, ch.bg) != (cur_fg, cur_bg):
                    buf.append("\x1b[" + ";".join(sgr(ch.fg, ch.bg)) + "m")
                    cur_fg, cur_bg = ch.fg, ch.bg
                buf.append(ch.data or " ")
                shown[y][x] = (ch.data, ch.fg, ch.bg)
                x += 1
        if buf:
            buf.append("\x1b[0m")
            out.append([round(at - last_t, 3), "o", "".join(buf)])
            last_t = at

    for i, (dt, kind, data) in enumerate(events):
        t += dt
        while t >= next_frame:
            frame(next_frame)
            next_frame += step
        if kind == "o" and i == last_output:
            # the exit's own output: the last frame, then it as it was
            frame(t)
            out.append([round(t - last_t, 3), "o", data])
            last_t = t
        elif kind == "o":
            stream.feed(data.encode())
        elif kind != "o":
            out.append([round(t - last_t, 3), kind, data])
            last_t = t
    with open(dst, "w") as f:
        f.write(json.dumps(header, separators=(",", ":")) + "\n")
        for e in out:
            f.write(json.dumps(e, ensure_ascii=False) + "\n")


if __name__ == "__main__":
    main()
