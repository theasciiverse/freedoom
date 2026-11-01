#!/bin/sh
# Fetches Freedoom 0.13.0 (BSD-3-Clause) into freedoom/: freedoom1.wad
# (Phase 1), freedoom2.wad (Phase 2), COPYING.txt and CREDITS.txt, checked
# against the SHA-256 Freedoom published with the release.
set -eu
cd "$(dirname "$0")/.."
version=0.13.0
sum=3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59
zip="freedoom-$version.zip"
mkdir -p freedoom
if [ ! -f "freedoom/$zip" ]; then
	curl -fsSL -o "freedoom/$zip" \
	  "https://github.com/freedoom/freedoom/releases/download/v$version/$zip"
fi
if command -v sha256sum >/dev/null; then
	echo "$sum  freedoom/$zip" | sha256sum -c -
else
	echo "$sum  freedoom/$zip" | shasum -a 256 -c -
fi
unzip -o -j -q "freedoom/$zip" "freedoom-$version/freedoom1.wad" \
  "freedoom-$version/freedoom2.wad" "freedoom-$version/COPYING.txt" \
  "freedoom-$version/CREDITS.txt" -d freedoom
