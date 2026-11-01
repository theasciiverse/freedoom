#!/usr/bin/env python3
"""Records asciiverse/freedoom.cast, the catalog preview, at 100x30
(hub/RECORDING.md): the staged game (asciiverse/stage.sh first), started by
its start script with a scratch HOME, in the half-block picture. It shows
Freedoom's title and then its own demos, the recorded games Doom plays
while nobody touches it, then quits through the menu. Sound is off
(-nosound), so recording plays nothing aloud; a cast carries no sound
anyway. Keys are sent as the kitty keyboard protocol sends them, press and
release. The game redraws 35 times a second, which makes a cast of well
over 100 MB, so the take is re-encoded at 12 frames a second
(asciiverse/recast.py, colours within 12 of the last frame kept), which
looks the same and comes to about 23 MB for 39 s. Needs pyte. Usage, from the repo
root:

    sh asciiverse/build.sh && sh asciiverse/freedoom.sh && sh asciiverse/stage.sh
    python3 asciiverse/record-preview.py
"""
import os, pty, select, shutil, signal, struct, sys, fcntl, tempfile, termios, time
import pyte

COLS, ROWS = 100, 30
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
cast = os.path.join(root, "asciiverse", "freedoom.cast")
raw = tempfile.mktemp(suffix=".cast")
home = tempfile.mkdtemp()
env = dict(os.environ, HOME=home, TERM="xterm-256color", COLORTERM="truecolor",
           LANG="C.UTF-8")
env.pop("XDG_DATA_HOME", None)

screen = pyte.Screen(COLS, ROWS)
stream = pyte.ByteStream(screen)

pid, fd = pty.fork()
if pid == 0:
    os.chdir(os.path.join(root, "stage"))   # the recording shows no local paths
    os.execvpe("asciinema", ["asciinema", "rec", "--overwrite", "-q", "-i", "2",
        "--window-size", f"{COLS}x{ROWS}", "--capture-env", "TERM,COLORTERM,LANG",
        "-c", "./freedoom -nosound", raw], env)
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))

done = False
def alive():
    global done
    if not done and os.waitpid(pid, os.WNOHANG)[0]: done = True
    return not done

def pump(t):
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.01)
        if r:
            try: stream.feed(os.read(fd, 1 << 20))
            except OSError: return

def tap(seq, hold=0.1, wait=0.6):
    """A key pressed and let go: seq is the kitty protocol's key part, such
    as '27' (Escape) or '1;{}A' for an arrow, with {} for the event type."""
    press = seq.format("1:1") if "{}" in seq else seq + ";1:1u"
    release = seq.format("1:3") if "{}" in seq else seq + ";1:3u"
    os.write(fd, b"\x1b[" + press.encode()); pump(hold)
    os.write(fd, b"\x1b[" + release.encode()); pump(wait)

def stop_game():
    """Last resort: end the game under asciinema so the recording closes."""
    kids, todo = [], [pid]
    while todo:
        p = todo.pop()
        try: ch = open(f"/proc/{p}/task/{p}/children").read().split()
        except OSError: ch = []
        kids += ch; todo += [int(c) for c in ch]
    for k in reversed(kids):
        try: os.kill(int(k), signal.SIGTERM)
        except ProcessLookupError: pass

pump(float(os.environ.get("DEMO_SECONDS", 36)))  # the title, then the demos
if alive():
    tap("27", wait=1.2)         # Escape: the menu
    tap("1;{}A", wait=0.8)      # Up: from New Game round to Quit Game
    tap("13", wait=1.5)         # Enter: are you sure?
    tap("121", wait=2.0)        # y
for _ in range(30):
    if not alive(): break
    pump(0.2)
if alive():
    stop_game(); pump(1.0)
shutil.rmtree(home, ignore_errors=True)
import subprocess
subprocess.run([sys.executable, os.path.join(root, "asciiverse", "recast.py"),
                raw, cast, "12", "12"], check=True)
os.remove(raw)
