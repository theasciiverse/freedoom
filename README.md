# Freedoom in the terminal

Freedoom's two complete games, played in your terminal in full colour, two
pixels to a character, with music, sound effects and gamepads. Packaged for
the [Asciiverse](https://asciiverse.sh) catalog.

- **Freedoom: Phase 1**: four episodes
- **Freedoom: Phase 2**: one long campaign of 32 levels

## Playing

Run `freedoom`. It plays Phase 1; `freedoom -iwad freedoom2.wad` plays
Phase 2. The bigger the terminal window, the sharper the picture. A terminal
that reports key releases (Ghostty, kitty, WezTerm, foot) gives exact
movement and lets Ctrl, Shift and Alt fire, run and strafe.

- **Arrow keys**: move and turn; **,** and **.**: strafe
- **Space**: fire; **E**: use doors and switches; **]**: run
- **1** to **7**: weapons; **Tab**: map; **Esc**: menu
- Gamepad, on Linux: left stick moves and strafes, right stick turns, **RT**
  fires, **LT** runs, **A** uses, **LB**/**RB** change weapons, **Back** is the
  map and **Start** the menu

Options: `-skill 1` to `-skill 5`, `-warp E M` (`-warp M` in Phase 2),
`-nosound`, `-nomusic`, and `-chars ascii|block|braille` for the engine's
other looks. Settings and saved games are kept in
`~/.local/share/freedoom`, where `.default.cfg` holds the key bindings.

## Building

    sh asciiverse/build.sh && sh asciiverse/freedoom.sh && sh asciiverse/stage.sh
    stage/freedoom

## Credits

- **Engine: [doom-ascii](https://github.com/wojciech-graj/doom-ascii)** by
  Wojciech Graj, the terminal port this is built on; its own README is
  [README.upstream.md](README.upstream.md). It is a port of
  [doomgeneric](https://github.com/ozkl/doomgeneric) (ozkl), from
  [Chocolate Doom](https://www.chocolate-doom.org) (Simon Howard and
  contributors), from the engine source id Software released in 1997.
- **Game data: [Freedoom](https://freedoom.github.io)**, by the Freedoom
  project (FREEDOOM-CREDITS.txt).
- **Music**: Chocolate Doom's OPL player with Nuked OPL3 (Alexey
  Khokholov); sound through [miniaudio](https://miniaud.io).
- Asciiverse's changes (sound, music, key releases, the half-block picture,
  gamepads) are listed in [PATCHES.md](PATCHES.md).

## Licences

The engine is GPL-2.0-or-later (LICENSE); Freedoom is BSD-3-Clause
(FREEDOOM-COPYING.txt).
