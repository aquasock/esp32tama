# TamaLib (vendored)

The first-gen Tamagotchi emulation library used by this project, from **TamaLib**
by Jean-Christophe Rona.

- Upstream: <https://github.com/jcrona/tamalib>
- Revision: `ce304d55f9a73c60232ce3f552e7983db3fa399c`
- Licence: **GPL-2.0-or-later**, full text in `LICENSE`. The "or later" clause
  is what makes it usable under this repository's GPL-3.0.
- Copyright (C) 2021 Jean-Christophe Rona <jc@rona.fr>

Deliberately hardware-agnostic: the whole port is one `hal_t` struct of about a
dozen callbacks. CPU, timers, interrupts and the segment-to-pixel mapping are
upstream's.

## What is here

```
cpu.c cpu.h        the E0C6S46/E0C6S48 CPU, timers and interrupts
hw.c hw.h          segment mapping, buttons, buzzer
tamalib.c/.h       the library entry points
hal.h              the abstraction a port implements
hal_types.h.template   integer widths; hal_types.h lives in core/
patches/           local changes, see below
```

## Local patches

Upstream is **not** untouched. Two changes, both in `patches/`:

**`0001-exec-counter.patch`** — a global instruction counter in `cpu_step()`,
used by the benchmark to report instructions per frame.

**`0002-init-step-index.patch`** — initialises `u8_t i = 0` in `cpu_step()`.
Upstream leaves it uninitialised; when the CPU is halted the opcode-lookup loop
never runs, but `i` is still read by the interrupt-dispatch test. A halted CPU's
interrupt behaviour therefore depended on stack garbage, and the same ROM halted
at different addresses in different builds. Found by noticing Angel halted at
`pc=0A7B` in one build and `pc=002A` in another.

## Segment maps

`hw.c`'s `seg_pos` table was replaced with **measured** data and a runtime
selector added (`hw_set_segmap`, exposed as `tamalib_set_segmap`).

The data comes from the SVG face artwork that ships inside the MAME ROM zips;
`assets/segmaps/` has the extraction code, the coordinate data and the
reasoning. Two results:

- **The whole Tamagotchi family shares one map.** P1, P2, Angel and Mothra
  produce byte-identical tables, 512/512 against TamaLib. The E0C6S46/E0C6S48
  split is not a segment-mapping difference.
- **Digimon is different, and upstream is wrong for it** in 448 of 512 entries.
  With TamaLib's table a Digimon ROM draws a plausible but incorrect creature;
  with the measured map it draws the right one. Corroborated 512/512 by an
  independent derivation from the ROM's own disassembly.

## Notes for a target port

- `cpu_set_speed(1)` makes the CPU pace itself in real time through the HAL's
  `sleep_until`/`get_timestamp`. `cpu_set_speed(0)` runs flat out. Do not pace
  the calling loop as well — two clocks fighting was a real bug here.
- `state_t` returned by `tamalib_get_state()` holds **pointers** to the live
  registers, not values. Dereference them when printing, or you will print
  recycled heap addresses that look like plausible nonsense.
- The free-running clock is 32,768 Hz. `tamalib_init(prog, NULL, freq)` takes
  the *timestamp* frequency, which is separate — 1,000,000 gives microsecond
  resolution.

## ROM policy

No ROM is distributed. See `docs/TAMAP1-DESKTOP.md` for what has been verified
against real dumps, and `assets/segmaps/` for the measured maps.
