# Pokémon Mini — desktop reference build

The Linux front end, which is the reference we judge the watch port against.
Same role `tamagui` plays for the V3: get it working and playable on a normal
machine first, then carry it across.

## Status

**Working.** Real retail cartridges boot and play. Verified by eye on a desktop
with the actual BIOS and nine cartridges: Pokémon Pinball, Tetris, Puzzle
Collection, Zany Cards, Lunch Time, and the Party Mini variants.

PKM Pinball screenshot check: bumpers, ball and score display render, and the
frame changes over time (three distinct frame hashes at 300 / 900 / 1800
emulated frames), so it is running rather than holding a static image.

## Build and run

```
./tools/build_pmgui.sh
./pmgui <bios> <cartridge> [--scale N]
```

Needs SDL2 and zlib development files.

- `<bios>` is a real BIOS dump. The free BIOS from PokeMini's tree runs homebrew
  but is not a substitute for retail cartridges; see *BIOS* below.
- `<cartridge>` may be a bare `.min` **or a `.zip` containing one**, which is how
  the MiSTer/MAME sets ship. No extraction step is needed.
- `--scale` defaults to 6, giving a 576×384 window; F1/F2 change it at runtime.

## Keys

```
arrows    d-pad
Z         A
X         B
C         C
Enter     Power
F5 / F9   save / load state
F1 / F2   window smaller / larger
M         mute
Esc       quit
```

Save states go to `<cartridge>.state` next to the cartridge, using PokeMini's
own snapshot format. Verified: PC `003313` → `032326` after 400 more frames →
restored to `003313` on load.

## Where ZIP support lives, and why there

In the **front end**, not the core. `frontends/pmgui.c` carries a small ZIP
reader using zlib's raw-deflate path. The core links against failing stubs
instead (`core/pokemini/minizip_stubs.c`), so the embedded build never carries
zlib — which matters when flash is the scarcest thing on the board.

The trade is deliberate: convenience on the desktop, no cost on the target.

## BIOS

Retail cartridges need a retail BIOS. The free BIOS that ships with PokeMini
(`core/pokemini/freebios.c`, 4 KB, GPL) idles at `$0040` and does not hand over
to a commercial cartridge, so it is useful for checking reset and the memory map
and not much else.

A real dump is 4096 bytes and starts `9A 00 AB 00 AB 00 CF 01`; the free BIOS
diverges at offset 6 (`... 2F 02`). Nothing from a BIOS dump is in this
repository and none should be.

## The bug that made the first harness lie

Worth repeating here because the front end had the same shape:
`PokeMini_SetMINMem()` only points the core at the image. Without
`PokeMini_Reset(0)` afterwards the CPU stays wherever `PokeMini_Create()` left
it, the cartridge never starts, and the core still executes happily — which
reads as a working emulator producing a suspiciously stable instruction count.
Both `pokemini_bench` and `pmgui` now reset after loading.

## What this does not yet do

- **No audio verification.** The path is wired (44.1 kHz, `MinxAudio_GetSamplesS16`
  into a queued SDL device) and the mute key works, but nothing has confirmed
  that the right sounds come out. That needs a listening test, which is the
  operator's job, not a build's.
- **No dirty-rectangle rendering.** It redraws all 96×64 every frame, which is
  free on a desktop. The watch port cannot afford that; see
  `POKEMINI-DISPLAY-GATE.md`.
- **No frame skip.** Pacing is a 72 Hz deadline with resynchronisation if more
  than one frame is missed, so a slow host runs behind rather than speeding the
  machine up.
- **Shades are flat.** The core computes a decayed intensity array
  (`LCDPixelsA`) for the original's fading dots; the front end draws
  `LCDPixelsD` as crisp black on white. Good for a reference, not faithful to
  the panel's grey.

  Measured, so nobody later mistakes it for a bug: `LCDPixelsD` is strictly
  binary — the histogram over a Pinball title screen is `0: 1935, 1: 4209` and
  nothing else. `LCDPixelsA` carries 4 distinct intensities. So the heavy
  checkerboard in the title art is the game's own spatial dithering, not a
  decoding fault, and drawing it one bit per dot is correct.
