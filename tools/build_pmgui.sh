#!/bin/sh
# build_pmgui.sh - build the SDL2 desktop front end for the Pokémon Mini core
#
#   ./tools/build_pmgui.sh
#
# Needs SDL2 and zlib development files. zlib is used only by the front end, to
# read the .zip sets people actually have; the core itself is built without it
# so the embedded target does not carry it.
set -e

CC=${CC:-gcc}
OUT=${OUT:-./pmgui}
ROOT=$(dirname "$0")/..

SDL_CFLAGS=$(pkg-config --cflags sdl2 2>/dev/null || echo "")
SDL_LIBS=$(pkg-config --libs sdl2 2>/dev/null || echo "-lSDL2")

$CC -O2 -DPERFORMANCE -w \
    $SDL_CFLAGS \
    -I"$ROOT/core/pokemini" \
    -o "$OUT" \
    "$ROOT/frontends/pmgui.c" \
    "$ROOT"/core/pokemini/*.c \
    $SDL_LIBS -lz

echo "built $OUT"
