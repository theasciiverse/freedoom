#!/bin/sh
# Builds doom-ascii with upstream's Makefile, natively: Linux (glibc, on the
# oldest runner, so it runs on glibc 2.35 and newer) and macOS (on a runner
# of each architecture). Leaves _unix/game/doom-ascii.
#
# Not static: miniaudio, the sound, loads the system's sound library (ALSA
# or PulseAudio, Core Audio) when the game starts, which a static musl
# binary cannot; without one the game plays on silently.
set -eu
cd "$(dirname "$0")/.."
rm -rf _unix
make -j4
strip _unix/game/doom-ascii
