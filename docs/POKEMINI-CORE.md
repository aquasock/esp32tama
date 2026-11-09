# Pokémon Mini core — vendoring and measurements

Status 2026-11-09. The core is vendored, builds headless, and runs real homebrew
ROMs. This page records provenance and the numbers, including one measurement
that was wrong and how it was caught.

## What is vendored

PokeMini by JustBurn, revision `8a64225e`, version 0.61, **GPL-3.0** — the same
licence as this repository. Source in `core/pokemini/`, provenance and the list
of dropped files in `core/pokemini/README.md`.

Dropped: the disassembler tables (59 KB, debugger-only, verified by link test
that the execution core does not reference them), every `platform/` front end,
the export and conversion tools, and zlib/minizip. `core/pokemini/unzip.h` plus
`minizip_stubs.c` provide the ZIP-loader symbols as failing stubs so the core
links without zlib.

Exactly one upstream file is modified, with the patch recorded:

```
core/pokemini/patches/0001-exec-counter.patch   +2 lines in MinxCPU_XX.c
```

That adds a global instruction counter so the harness can report instructions
per frame. It is two lines and carries no behaviour change.

## Build

```
./tools/build_pokemini.sh
./pokemini_bench <rom.min> [frames]
```

`-DPERFORMANCE` matters: it inlines the PRC's memory access instead of going
through call pointers. Measured on the host with `bouncing`, same ROM and
compiler flags: **77.8 M steps/sec without it, 96.7 M with it** — about 24 %.

## The bug that nearly produced a false result

The first harness loaded a ROM and reported a flat **6,945 instructions per
frame for every cartridge tested**. That looked like a good result: 50 % of the
13,889-per-frame budget, consistent, plausible.

It was measuring the free BIOS idle loop. `PokeMini_SetMINMem()` only points the
core at the image; it does not reset the CPU. Without `PokeMini_Reset(0)` after
loading, the CPU is wherever `PokeMini_Create()` left it and the cartridge never
starts — but the core still executes instructions and still reports a tidy
number. The PC sat at `$0040`, in the BIOS.

Two things caught it:

1. **All five cartridges produced identical counts.** Real code does not do
   that. A constant across different programs is a signal, not a result.
2. Instrumenting the PC per frame showed it parked in BIOS space rather than in
   the cartridge at `$2100` and above.

The fix is one line, and it is what every real front end does. It is now
commented in `tools/pokemini_bench.c` so it cannot be quietly dropped again.

Related, and equally worth recording: `PokeMini_EmulateFrame()` **cannot be used
here**. It waits on a vertical blank that the free BIOS never produces, so the
first version of the harness hung. The harness slices by cycles instead — 55,555
per frame, 4 MHz ÷ 72 Hz — which is also what a target port wants, since there
is no vsync to wait for on the watch either.

## Measurements

Host: x86-64, `-O2 -DPERFORMANCE`. The five homebrew examples that ship with
PokeMini's dev kit, all GPL with source, 4,000 frames each.

| ROM | Instructions/frame | Budget used |
|---|---|---|
| `mapview` | 4,600 | 33 % |
| `bouncing` | 4,648 | 33 % |
| `sound_tst` | 4,653 | 34 % |
| `framebuffer` | 6,619 | 48 % |
| `template` (`main`) | 6,945 | 50 % |

The budget is 13,889 instructions per frame: 4 MHz ÷ 4-cycle fetch = 1 MIPS,
÷ 72 Hz. So every example sits at a third to a half of full CPU.

**Timing needs a long run.** The same binary and ROM reports 107 M
instructions/sec at 200 frames, 177 M at 2,000 and 194 M at 20,000 — short runs
measure the host's frequency ramp, not the core. The harness now defaults to
4,000 frames and anything under a second of wall time should be treated as
noise.

Caveat on the table: these are demo programs, not retail games. `framebuffer`
and `template` are the heavier end at half the budget, so treat 50 % as a
plausible ceiling for a simple title rather than a measured bound for something
like Pokémon Pinball. The 13,889 budget is a hard ceiling by definition — a
title cannot exceed it and still run at full speed on real hardware.

## What this means for the port

Extrapolating the host figure to the ESP32-S3 is arithmetic, not measurement,
and is stated as such. The core runs at **107–194 M instructions/sec** on this
machine depending on run length; call it 150 M as a working figure. The
ESP32-S3's Xtensa LX7 at 240 MHz is roughly 20–40× slower than a desktop x86
core on integer code of this shape, which puts it at **3.75–7.5 M
instructions/sec — 3.75× to 7.5× realtime.**

Even the pessimistic end leaves headroom, and crucially the CPU is *not* the
binding constraint: the display path is. See `POKEMINI-DISPLAY-GATE.md`.

This must still be measured on the target before any UI work. The whole point
of doing the gate first is that an estimate is not a measurement.

## Next steps

1. Cross-compile `tools/pokemini_bench.c` for the ESP32-S3 and run it there.
   That converts the extrapolation above into a number.
2. Confirm the display-path numbers from `POKEMINI-DISPLAY-GATE.md` once the
   board is free.
3. Only then start the front end.
