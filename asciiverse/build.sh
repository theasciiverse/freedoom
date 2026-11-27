#!/bin/sh
# Builds doom-ascii with upstream's Makefile, natively: Linux (glibc, on the
# oldest runner, so it runs on glibc 2.35 and newer) and macOS (on a runner
# of each architecture). Leaves _unix/game/doom-ascii.
#
# Not static: miniaudio, the sound, loads the system's sound library (ALSA
# or PulseAudio, Core Audio) when the game starts, which a static musl
# binary cannot; without one the game plays on silently.
#
# The chafa picture is linked in from static archives (chafa, GLib), so it
# needs nothing on the player's machine: required on Linux, where Ubuntu's
# -dev packages carry the archives; on macOS only when Homebrew's do, and
# said here when they do not (asciiverse/static-libs.sh).
set -eu
cd "$(dirname "$0")/.."
rm -rf _unix
chafa="AV_CHAFA=1 AV_CHAFA_STATIC=1"
if [ "$(uname)" = Darwin ] && ! sh asciiverse/static-libs.sh chafa >/dev/null; then
    echo "build.sh: no static chafa here, building without the chafa picture"
    chafa="AV_CHAFA=0"
fi
# shellcheck disable=SC2086
make -j4 $chafa AV_CACA=0
# for the release's smoke test: this build has the chafa picture
[ "$chafa" = AV_CHAFA=0 ] || touch _unix/with-chafa
strip _unix/game/doom-ascii
