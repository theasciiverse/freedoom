# Changes from upstream

This is the Asciiverse packaging of doom-ascii 0.3.2 (GPL-2.0-or-later) by
Wojciech Graj (github.com/wojciech-graj/doom-ascii), a port of the Doom
engine (doomgeneric, from Chocolate Doom) to the terminal, played with
Freedoom's free game data (BSD-3-Clause) instead of id Software's. `main`
starts at upstream's `v0.3.2` tag, with upstream's history; the `upstream`
remote points there.

It is listed as **Freedoom**, the game it plays (the command is `freedoom`),
and credits doom-ascii as its engine; no id Software data is shipped or
needed. Upstream's README is kept as `README.upstream.md`; `README.md` is
this edition's.

## The game

Upstream's engine and its look are kept (`-chars ascii`, `block` and
`braille` work as before). Added, for terminals of the kind the catalog
asks for (24-bit colour, Unicode, and ideally the kitty keyboard protocol,
as Ghostty, kitty, WezTerm, foot and others have):

- **Key releases.** A terminal reports presses, and repeats while a key is
  held, but not releases, and Doom needs to know when a key is let go.
  doom-ascii guessed with one timer (`-kpsmooth`, 42 ms): too short to
  bridge the half second before a held key starts repeating, so walking
  stuttered, and it read 15 bytes a frame and threw the rest away. Now
  (`src/asciiverse/av_input.c`): terminals with the kitty keyboard protocol
  report every press and release (the game asks with `CSI > 11 u` and undoes
  it at exit), so movement is exact, and Shift, Ctrl and Alt become keys;
  elsewhere a key counts as held for 550 ms after a fresh press, then for
  100 ms after each repeat. All waiting input is read each frame, and a
  sequence split between two reads is kept for the next. Upstream's way is
  still there: passing its `-kpsmooth` option selects it.
- **The half-block picture** (`-chars half`, `src/asciiverse/av_video.c`):
  each cell shows two pixels, the upper half block in the upper pixel's
  colour on the lower pixel's colour, so pixels are square and in full
  colour. The 320x200 frame (`-scaling 1`) is fitted to the terminal at
  Doom's 4:3, centred, each pixel the average of the frame's pixels it
  covers, and follows a resized terminal. Only changed cells are sent.
- **Sound and music.** doom-ascii had none. Sound effects are mixed from the
  game's sound lumps; music is Doom's MUS played through the GENMIDI
  instruments on an emulated OPL3, as a Sound Blaster would, with Chocolate
  Doom 3.1.1's OPL music player, OPL library and Nuked OPL3. Both go out
  through miniaudio, which loads the system's sound library when the game
  starts and plays on silently without one (`src/asciiverse/av_audio.c`;
  `src/asciiverse/README.md` lists every file and its source).
  `AV_AUDIO_DUMP=<file>` also writes the stream to a file (raw 16-bit
  stereo), to check the sound without listening.
- **Gamepads, on Linux** (`src/asciiverse/av_pad.c`). doom-ascii had no
  joystick: the engine's input for one (`ev_joystick`) was there, but its
  SDL reader is left out of the build. A terminal cannot report a pad, so
  the game opens one itself from `/dev/input/event*`, with no library
  (desktops give the person at the machine access to joysticks). Only real
  gamepads are used, devices with gamepad or joystick buttons (0x120 to
  0x13f) and none of a digitizer's (0x140 to 0x14f): tablet pads and 3D
  mice are joystick devices too, and their resting axes would turn the
  player without end. A pad plugged in or out while the game runs is found
  or let go within a second or two. The layout: left stick to move and
  strafe, right stick to turn, d-pad as the arrow keys, RT fire, LT run,
  A use, LB and RB previous and next weapon, Back the map, Start the menu.
  The sticks are analog (`g_game.c`): the engine only knew a joystick
  pushed or not; turning goes with the square of the push, up to the
  keyboard's fast turn, so a small push aims finely. While a menu is open
  the pad sends the menu's keys instead (A chooses, or answers yes to a
  prompt; B goes back, or answers no), and a game saved from the pad in an
  empty slot is named after its level (`m_menu.c`), since a pad cannot type
  a name. The engine's `joyb_` settings named SDL's buttons; they are set
  to the pad's at start. With stderr redirected, every pad button, device
  and menu change is logged there.

The code changes outside `src/asciiverse/` only add, and only on Unix, so
upstream's Windows build and its CI are as they were: `Makefile` (the new
sources, `-DAV_SOUND`, `-lpthread -ldl`), `src/i_sound.c` (the two modules
in its module lists, under `AV_SOUND`) and `src/doomgeneric_ascii.c`
(`-chars half`, and the key path switched to `av_input.c` at run time),
and for the gamepad `src/i_joystick.c` and `src/i_video.c` (reading it each
tic, as Chocolate Doom does), `src/g_game.c` (analog sticks) and
`src/m_menu.c` (save names), all under `AV_GAMEPAD`.

## Packaging (new files)

- `asciiverse/freedoom`: the start script, installed as `freedoom` with the
  binary as `lib/freedoom`. The game keeps `.default.cfg`
  and saved games where it runs, so it runs from
  `${XDG_DATA_HOME:-~/.local/share}/freedoom` (a `doom-ascii` folder from
  before the rename is moved there), where the script puts
  the engine's key settings on the first run. It plays
  Freedoom: Phase 1 with `-chars half -scaling 1` unless told otherwise;
  `-iwad freedoom2.wad` finds Phase 2 beside the script.
- `asciiverse/freedoom.sh`: fetches Freedoom 0.13.0 and checks it against
  its published SHA-256; `asciiverse/build.sh`, `asciiverse/stage.sh`:
  the build and the av archive.
- `asciiverse/record-preview.py`: records the catalog preview;
  `asciiverse/test-pad.c`: a virtual gamepad, to test the pad without one.
- `.github/workflows/release.yml`: builds it all on a tag. Upstream's
  `ci.yml` (a build on each push) is left as it is.

## Not changed

- The bindings are upstream's: arrows to move, `,` and `.` to strafe,
  Space to fire, `E` to use, `]` to run, `1`-`7` for weapons.
- No Windows build here (upstream's builds Windows; the half picture and
  the new keyboard are Unix only).
