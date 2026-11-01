# doom-ascii (Asciiverse packaging)

doom-ascii 0.3.2 by Wojciech Graj, the Doom engine in the terminal, played
with Freedoom (BSD-3-Clause game data, fetched at build time), packaged for
the Asciiverse catalog under upstream's name. Upstream is active
(github.com/wojciech-graj/doom-ascii, remote `upstream`); `main` is their
`v0.3.2` tag plus ours. PATCHES.md lists every change.

- Ours: exact key releases (kitty keyboard protocol, else inferred),
  the half-block picture (`-chars half`), sound effects and OPL music via
  miniaudio and Chocolate Doom 3.1.1's OPL code. All in `src/asciiverse/`
  (its README.md lists the files and sources), hooked in by small marked
  edits to `Makefile`, `src/i_sound.c`, `src/doomgeneric_ascii.c`.
- Name: listed as doom-ascii (upstream's), never with "Doom" in a title of
  our own; it plays Freedoom, no id data. Decided with David 2026-11-01.
- `origin` is to be github.com/theasciiverse/doom-ascii. Release: tag
  `v0.3.2-av.<n>` and push it.
- Build: `sh asciiverse/build.sh` (needs a C compiler and make), then
  `sh asciiverse/freedoom.sh && sh asciiverse/stage.sh`. Linux builds are
  glibc, not static: miniaudio loads ALSA/PulseAudio at run time.
- Testing sound without speakers: build with
  `CFLAGS="-DMA_ENABLE_ONLY_SPECIFIC_BACKENDS -DMA_ENABLE_NULL" make PLATFORM=nulltest`
  and run with `AV_AUDIO_DUMP=<file>` (raw s16 stereo, 44.1 kHz).
- The start script runs the game from ~/.local/share/doom-ascii (its config
  and saves go to the current folder).
- Preview: `python3 asciiverse/record-preview.py` (needs pyte).
