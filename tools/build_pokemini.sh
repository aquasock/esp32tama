#!/bin/sh
# build_pokemini.sh - build the headless PokeMini harness
#
# The Pokemon Mini core is vendored under core/pokemini. -DPERFORMANCE is not
# optional: it inlines the PRC's memory access instead of going through call
# pointers, which measured ~24% faster on the host (77.8 -> 96.7 M steps/sec).
# A target port wants it for the same reason.
#
#   ./tools/build_pokemini.sh            # host build
#   CC=riscv64-unknown-linux-gnu-gcc ./tools/build_pokemini.sh
set -e

CC=${CC:-gcc}
OUT=${OUT:-./pokemini_bench}
ROOT=$(dirname "$0")/..

$CC -O2 -DPERFORMANCE -w \
    -I"$ROOT/core/pokemini" \
    -o "$OUT" \
    "$ROOT/tools/pokemini_bench.c" \
    "$ROOT"/core/pokemini/*.c

echo "built $OUT"
