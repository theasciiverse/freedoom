# src/asciiverse

What the Asciiverse packaging adds to doom-ascii's source (see PATCHES.md
at the top of the repository for what it changes and why).

| File | What it is | From | Licence |
|---|---|---|---|
| `av_input.c` | Keyboard input on Unix terminals: the kitty keyboard protocol's exact presses and releases, or releases inferred from key repeats | new | public domain |
| `av_video.c` | The half-block picture (`-chars half`): two 24-bit colour pixels per cell, fitted to the terminal at 4:3 | new | public domain |
| `av_pad.c` | Gamepads on Linux from the kernel's event devices: analog sticks in play, the menu's keys in menus | new | public domain |
| `av_audio.c` | One miniaudio stream: the sound-effect mixer (`sound_av_module`) and the OPL driver for the music (`opl_av_driver`, a port of Chocolate Doom's `opl/opl_sdl.c`) | new, OPL driver from Chocolate Doom | GPL-2.0-or-later |
| `av_compat.h` | What the Chocolate Doom files expect from its headers (`boolean`, `PACKED_STRUCT`, `I_Realloc`, ...) | new | public domain |
| `miniaudio.h` | Audio playback library, version 0.11.25 | github.com/mackron/miniaudio | public domain or MIT-0 |
| `i_oplmusic.c` | The OPL music player (`music_opl_module`): Doom's MUS music through the GENMIDI instruments on an OPL chip | Chocolate Doom 3.1.1, unchanged | GPL-2.0-or-later |
| `midifile.c`, `midifile.h`, `mus2mid.c`, `mus2mid.h` | MIDI file reader and MUS to MIDI converter, for the player | Chocolate Doom 3.1.1, unchanged | GPL-2.0-or-later |
| `opl.c`, `opl.h`, `opl_internal.h`, `opl_queue.c`, `opl_queue.h` | The OPL library | Chocolate Doom 3.1.1; `opl.c` uses miniaudio instead of SDL and has the one driver | GPL-2.0-or-later |
| `opl3.c`, `opl3.h` | Nuked OPL3, the OPL3 emulator, by Alexey Khokholov | Chocolate Doom 3.1.1, unchanged | GPL-2.0-or-later |

Chocolate Doom 3.1.1 is commit `410d96855b5df5410ff591a90efeafa889119224`
of github.com/chocolate-doom/chocolate-doom (tag `chocolate-doom-3.1.1`).
