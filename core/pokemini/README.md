# PokeMini core (vendored)

The Pokémon Mini emulation core used by this project, taken from **PokeMini** by
JustBurn.

- Upstream: <https://github.com/pokemon-mini/PokeMini>
- Revision: `8a64225e21307ee89b18feca0a429a8d2d43e6d1` (2026-10-31)
- Upstream version: 0.61
- Licence: **GPL-3.0**, same as this repository. Full text in
  `LICENSE-PokeMini`.

## What is here

Every file in this directory except `minizip_stubs.c` and this README is
**verbatim upstream**, copied without modification. That is deliberate: it keeps
upstream fixes trivial to pull, and it makes the provenance auditable by diffing
against the revision above.

From `source/`:

```
PokeMini.c  PokeMini.h  PokeMini_Version.h      top level, Create/EmulateCycles
Hardware.c  Hardware.h                          the per-cycle scheduler
CommandLine.c/.h  Multicart.c/.h  PMCommon.c/.h
MinxCPU*.c/.h                                   the S1C88 CPU (XX/CE/CF/SP tables)
MinxIO.c  MinxIRQ.c  MinxPRC.c  MinxColorPRC.c  ports, interrupts, PRC
MinxLCD.c  MinxAudio.c  MinxTimers.c
Endianess.h  IOMap.h  MinxCPU_noBranch.h
Video.c  Video_x1.c                             framebuffer conversion
```

From `resource/`:

```
PokeMini_ColorPal.c/.h                          palette tables
```

From `freebios/`:

```
freebios.c/.h                                   the free BIOS, GPL, 4 KB
```

## What was dropped, and why

- **`sourcex/InstructionProc.c` and `sourcex/InstructionInfo.c`** (59 KB). These
  are the disassembler tables, used only by the debugger build. Verified by
  link test that the execution core does not reference them.
- **Every `platform/` directory.** Those are the SDL, NDS, PSP, Wii, Dreamcast,
  Dingoo, Windows and GTK front ends. This project has its own.
- **`sourcex/` export tools**, the music and image converters, `dependencies/`.

## What is ours

`minizip_stubs.c` — PokeMini can open a `.min` that is really a ZIP, which pulls
in minizip and zlib. Neither is needed for a bare cartridge image and neither
belongs on the target. PokeMini calls those symbols unconditionally, so the
stubs exist to satisfy the link; each fails the way an unreadable archive would.

## Building

```
gcc -O2 -DPERFORMANCE -Icore/pokemini \
    tools/pokemini_bench.c core/pokemini/*.c -o pokemini_bench
```

`-DPERFORMANCE` inlines the PRC's memory access instead of going through call
pointers. Measured on the host with the `bouncing` homebrew ROM: **77.8 M
steps/sec without it, 96.7 M with it**, about a 24 % gain. Use it for the target
too.

## The free BIOS

`freebios/freebios.min` is a 4 KB GPL-licensed BIOS written for PokeMini so that
the core can be exercised without any commercial ROM. It is embedded in
`freebios.c` as a byte array, so nothing extra needs to ship.

It is not a real BIOS: it idles at `$0040` and does no work. Useful for checking
that reset and the memory map are sane; useless for measuring speed. For that,
use a homebrew ROM — PokeMini's dev kit ships several with source under the same
licence.

## ROM policy

Same as the rest of this project: **no commercial ROM is ever distributed**.
The free BIOS ships because it is GPL. Retail `.min` files do not.
