#!/bin/sh
# build_tamap1.sh - build the SDL2 desktop front end for the original
# Tamagotchi (P1/P2, Epson E0C6S46)
#
#   ./tools/build_tamap1.sh
#
# TamaLib is vendored in core/tamalib (GPL-2.0-or-later, taken under GPL-3.0).
# Needs SDL2 development files.
set -e

CC=${CC:-gcc}
OUT=${OUT:-./tamap1}
ROOT=$(dirname "$0")/..
SDL_CFLAGS=$(pkg-config --cflags sdl2 2>/dev/null || echo "")
SDL_LIBS=$(pkg-config --libs sdl2 2>/dev/null || echo "-lSDL2")

$CC -O2 -w \
    $SDL_CFLAGS \
    -I"$ROOT/core" -I"$ROOT/core/tamalib" \
    -o "$OUT" \
    "$ROOT/frontends/tamap1gui.c" \
    "$ROOT"/core/tamalib/*.c \
    $SDL_LIBS

echo "built $OUT"
