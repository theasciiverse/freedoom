#!/bin/sh
# Prints the link flags for a pkg-config package with its libraries taken
# from their static archives (lib<name>.a), so the binary does not need
# them on the player's machine. Used for chafa and the GLib under it:
#
#   sh asciiverse/static-libs.sh chafa
#
# The system's own libraries stay as they are (libc, libm, pthread, dl, rt,
# resolv, and iconv on macOS); every other library must have a static
# archive, or this fails rather than quietly linking it dynamically.
# Archives are looked for in pkg-config's -L folders, then wherever the
# compiler finds its own (that is where libatomic.a lives).
set -eu
pkg=$1
cc=${CC:-cc}
flags=$(pkg-config --static --libs "$pkg")

dirs=
for f in $flags; do
    case $f in -L*) dirs="$dirs ${f#-L}" ;; esac
done

out=
seen=
for f in $flags; do
    case $f in
    -L*) ;;
    -l*)
        name=${f#-l}
        case " $seen " in *" $name "*) continue ;; esac
        seen="$seen $name"
        case $name in
        c|m|pthread|dl|rt|resolv|iconv) out="$out $f"; continue ;;
        esac
        a=
        for d in $dirs; do
            if [ -f "$d/lib$name.a" ]; then a="$d/lib$name.a"; break; fi
        done
        if [ -z "$a" ]; then
            p=$($cc -print-file-name="lib$name.a")
            [ -f "$p" ] && a=$p
        fi
        if [ -z "$a" ]; then
            echo "static-libs.sh: no lib$name.a for $pkg" >&2
            exit 1
        fi
        out="$out $a"
        ;;
    *) out="$out $f" ;;
    esac
done
echo $out
