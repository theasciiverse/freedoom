#!/bin/sh
# Lays out the av archive in stage/: the start script at the top, the
# binary in lib/, Freedoom's two games beside it (asciiverse/freedoom.sh
# first), doom-ascii's key settings (default.cfg, copied to the player's
# folder on the first run), and the licences and credits.
set -eu
cd "$(dirname "$0")/.."
rm -rf stage && mkdir -p stage/lib
cp asciiverse/doom-ascii stage/doom-ascii
cp _unix/game/doom-ascii stage/lib/doom-ascii
cp src/.default.cfg stage/default.cfg
cp freedoom/freedoom1.wad freedoom/freedoom2.wad stage/
cp freedoom/COPYING.txt stage/FREEDOOM-COPYING.txt
cp freedoom/CREDITS.txt stage/FREEDOOM-CREDITS.txt
cp README.md LICENSE PATCHES.md stage/
