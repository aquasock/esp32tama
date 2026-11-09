#!/bin/sh
# Build the SPLB32 core and its front ends.
#
#   ./build.sh                                            # host build
#   CC=xtensa-esp32s3-linux-uclibcfdpic-gcc ./build.sh    # cross build
#
# No ROM is included in this repository. Put your own dump at ./rom.bin
# (524288 bytes) next to the binaries.
set -e
CC="${CC:-gcc}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CORE="$HERE/../core"
FE="$HERE/../frontends"
cd "$HERE"

echo "compiler: $CC"

CORE_SRC="$CORE/splb32.c $CORE/ht24lc08.c $CORE/con_v3_ir.c $CORE/splb32_sound.c"

# Benchmark and trace driver.
$CC -O2 -Wall -Wextra -I"$CORE" -I"$FE" -o corebench "$FE/corebench.c" $CORE_SRC

# Framebuffer demo (Linux fbdev).
$CC -O2 -Wall -Wextra -I"$CORE" -I"$FE" -o tamafb "$FE/tamafb.c" "$FE/segmap.c" $CORE_SRC

# Desktop front end (SDL2). Optional: the watch build does not need it.
if pkg-config --exists sdl2 2>/dev/null; then
    $CC -O2 -Wall -Wextra -I"$CORE" -I"$FE" -o tamagui "$FE/tamagui.c" "$FE/segmap.c" $CORE_SRC \
        $(pkg-config --cflags --libs sdl2) -lm
else
    echo "skipping tamagui (no sdl2 pkg-config)"
fi

ls -l corebench tamafb tamagui 2>/dev/null || true
echo "built."

if [ ! -f rom.bin ]; then
    echo
    echo "NOTE: no rom.bin here. The emulator needs a Tamagotchi Connection V3"
    echo "      dump (524288 bytes) to run. This repository does not include"
    echo "      one; see README.md."
fi
