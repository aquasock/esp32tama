#!/bin/sh
# build_pokemini_target.sh - cross-compile the benchmark for the T-Watch
#
# Produces a static Xtensa binary that needs nothing from the watch's libc, so
# it can be copied over and run with no dependencies.
#
#   ./tools/build_pokemini_target.sh
#   ESCAPEMENT=/path/to/Escapement ./tools/build_pokemini_target.sh
set -e

ESCAPEMENT=${ESCAPEMENT:-../Escapement}
OUT=${OUT:-./for-escapement/pokemini_bench_target}
ROOT=$(dirname "$0")/..

TC="$ESCAPEMENT/build/toolchain/crosstool-NG/builds/xtensa-esp32s3-linux-uclibcfdpic"
CC="$TC/bin/xtensa-esp32s3-linux-uclibcfdpic-gcc"
DYN="$ESCAPEMENT/build/toolchain/xtensa-dynconfig/esp32s3.so"

[ -x "$CC" ] || { echo "no cross compiler at $CC" >&2; exit 1; }
[ -f "$DYN" ] || { echo "no $DYN" >&2; exit 1; }

# NOT optional: without this the compiler emits big-endian objects and the link
# fails against the little-endian uClibc sysroot with "file in wrong format".
# See Escapement/docs/toolchain.md.
export XTENSA_GNU_CONFIG=$DYN

"$CC" -Os -static -DPERFORMANCE -w \
    -I"$ROOT/core/pokemini" \
    -o "$OUT" \
    "$ROOT/tools/pokemini_bench.c" \
    "$ROOT"/core/pokemini/*.c

STRIP="$TC/bin/xtensa-esp32s3-linux-uclibcfdpic-strip"
[ -x "$STRIP" ] && "$STRIP" "$OUT"      # ~43 KB smaller, and nothing reads the symbols

echo "built $OUT"
"$TC/bin/xtensa-esp32s3-linux-uclibcfdpic-size" "$OUT"
file "$OUT" | cut -c1-100
