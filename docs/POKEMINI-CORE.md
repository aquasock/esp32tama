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

The host figure cannot be scaled to the target by a ratio — any such ratio would
be invented. What can be said is a **conditional bound**, which is enough to
decide whether to keep going:

- Realtime needs 1 M emulated instructions per second.
- That leaves **240 cycles per emulated instruction** at 240 MHz.
- A large-switch interpreter costs tens of cycles per emulated instruction, not
  hundreds. On this host the core runs at ~150 M instructions/sec on a core
  clocking several GHz, which is on the order of **20–40 cycles per emulated
  instruction** — so the instruction count per emulated instruction is in the
  tens, comfortably.

If the Xtensa port lands anywhere in the same order, realtime follows. It would
have to be *six to ten times* worse than the x86 codegen to miss, which is not a
plausible gap for straightforward C. But this is reasoning, not a measurement,
and it is exactly the kind of reasoning that produced the Tama-Go detour.

Even so, the CPU is *not* the binding constraint — the display path is. See
`POKEMINI-DISPLAY-GATE.md`. That is why the remaining risk sits in the panel,
not here.

This must still be measured on the target before any UI work.

## Target build — verified, not yet timed

The core cross-compiles for Xtensa/ESP32-S3 with the PlatformIO toolchain
(`toolchain-xtensa-esp-elf`, GCC 14.2.0). Every vendored source file compiles
clean. Reproduce with:

```
./tools/pokemini_size.sh
```

```
Xtensa ESP32-S3 footprint, -O2 -DPERFORMANCE:
  114,708 text    41 data    14,984 bss    129,733 total
```

Largest contributors: `MinxCPU_CE` 18.1 KB, `MinxCPU_XX` 15.2 KB,
`CommandLine` 15.1 KB, `PokeMini` 20.7 KB, `MinxCPU_CF` 7.9 KB.

`-DPERFORMANCE` costs **1,220 bytes of extra text** (114,708 vs 113,488) for a
measured 24 % speed gain on the host. That is obviously the right trade and
should be on for the target.

Two notes for a target port:

- **`CommandLine` is 15 KB of config parser** that a shipped emulator does not
  need. It is not free to remove — `PokeMini_Create` reads
  `CommandLine.synccycles` — but a target port that hardcodes those fields
  would recover most of it.
- **129 KB total** is comfortable next to Linux and the display driver on a
  16 MB flash part, but it is text, so it lives in flash rather than RAM. Only
  the 15 KB of `.bss` (framebuffers, RAM buffers) is actual memory pressure.

### What is still not measured

**The frame rate on the watch.** No Xtensa simulator is available here (no
`qemu-system-xtensa`, and `perf`/`valgrind` are both blocked on this host), and
the board is in use by the display-driver work, so there is no way to time the
core on target from this session.

This matters more than it sounds, because the obvious shortcut — "it already
runs on a 67 MHz ARM9, so it must be fast enough" — does not hold up. The NDS
port emulates a frame and then waits for the next 72 Hz tick with **no frameskip
and no catch-up**, so a console that cannot finish inside 13.9 ms runs slow
silently. An existing port proves portability, not speed. See the correction
note in `POKEMINI-SCOPE.md`.

## Next steps

1. Time the core on the watch. The core compiles for the target (above); what
   is missing is a run. Either build the `xtensa-esp32s3-linux-uclibcfdpic`
   toolchain described in `Escapement/docs/toolchain.md` and cross-compile the
   harness, or add a few lines to an existing on-device binary. This is the only
   step that converts the extrapolation into a measurement.
2. Confirm the display-path numbers from `POKEMINI-DISPLAY-GATE.md` once the
   board is free.
3. Only then start the front end.
