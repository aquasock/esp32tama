#!/bin/sh
# pokemini_size.sh - measure the core's footprint for the target CPU
#
# Cross-compiles every vendored PokeMini source file for Xtensa/ESP32-S3 and
# totals .text/.data/.bss. This is the flash-and-RAM question: the core has to
# fit alongside Linux and the display driver on the watch.
#
#   ./tools/pokemini_size.sh
#   XTENSA_GCC=/path/to/xtensa-esp32s3-elf-gcc ./tools/pokemini_size.sh
#
# It measures code size only. It says nothing about speed - see
# docs/POKEMINI-CORE.md for why speed has to be measured on the board.
set -e

GCC=${XTENSA_GCC:-xtensa-esp32s3-elf-gcc}
SIZE=${XTENSA_SIZE:-xtensa-esp32s3-elf-size}
ROOT=$(dirname "$0")/..
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

command -v "$GCC" >/dev/null 2>&1 || {
    echo "no $GCC on PATH; set XTENSA_GCC, or see" >&2
    echo "Escapement/docs/toolchain.md for building one" >&2
    exit 1
}

for f in "$ROOT"/core/pokemini/*.c; do
    "$GCC" -O2 -DPERFORMANCE -w -I"$ROOT/core/pokemini" \
           -c "$f" -o "$TMP/$(basename "$f" .c).o"
done

echo "Xtensa ESP32-S3 footprint, -O2 -DPERFORMANCE:"
"$SIZE" -t "$TMP"/*.o | tail -1

echo
echo "largest contributors:"
"$SIZE" "$TMP"/*.o | sort -k1 -n -r | head -6
