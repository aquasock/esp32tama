# Tamagotchi P1/P2 — desktop build

The original 1996 Tamagotchi, running on the Epson E0C6S46, on a normal Linux
desktop.

```
./tools/build_tamap1.sh
./tamap1 <rom.bin> [--scale N]
```

Needs SDL2. The operator supplies their own ROM; none is distributed here.

## Status

**Working.** The egg displays and animates, and the buttons respond — pressing
MIDDLE took the panel from 68 to 131 lit dots, which is a menu opening.

Verified:
- CPU walks the ROM correctly (PC, registers and stack all move over 20M steps)
- 5,000,000 instructions ≈ 14 emulated minutes (tick counter / 32768)
- display animates: 72 → 74 → 68 lit dots across 57 emulated minutes
- buttons work: MIDDLE opens a menu, LEFT/RIGHT/TAP all change the panel

Not verified:
- **Hatching.** The pet is asleep in the egg for the first several minutes, and
  driving it by poking buttons in a test harness disturbs it. This needs a
  real play session, not a benchmark.
- **The 8 icons never lit** in 57 emulated minutes of unattended running. That
  is probably correct for a freshly-hatched-egg state (no hunger, no discipline
  call, no attention needed), but it is unconfirmed. Worth watching in play.

## Vendored core

`core/tamalib/` — **TamaLib** by Jean-Christophe Rona,
<https://github.com/jcrona/tamalib>, revision `ce304d55`, GPL-2.0-**or-later**.

That "or later" matters: TamaLib is GPL-2.0 but explicitly permits any later
version, so it can be taken under this repository's GPL-3.0. The full GPL-2.0
text is in `core/tamalib/LICENSE`.

Verbatim upstream except for nothing — every file is unmodified. `hal_types.h`
is generated from upstream's `hal_types.h.template` and lives in `core/` because
`hal.h` includes `"../hal_types.h"`.

## What the port cost

One `hal_t` struct, about a dozen callbacks:

```
malloc free halt is_log_enabled log sleep_until get_timestamp
update_screen set_lcd_matrix set_lcd_icon set_frequency play_frequency handler
```

Everything else — CPU, timers, interrupts, the segment-to-pixel mapping — is
TamaLib's. `hw.c` does the 51-segment E0C6S48 / 40-segment E0C6S46 mapping and
hands this HAL a clean 32×16 matrix plus 8 icon flags.

That is the whole reason this platform took hours rather than days: the
hardware abstraction was already the porting interface.

## ROM format

First-gen Tamagotchi ROMs are stored as two bytes per **12-bit** word, with the
high nibble of the first byte unused:

```c
prog[i] = (u12_t)(b1 | ((b0 & 0x0F) << 8));
```

12,288 bytes therefore holds 6,144 words. That convention comes from TamaTool's
loader (`src/program.c`), not from guessing at the file.

The asset files in a typical MiSTer set (`boot1.rom`, `boot2.rom`) are **not**
ROMs — they are background and sprite artwork in raw RGB565 and alpha. Only
`rom.bin` is the program.

## What devices it runs

These are virtual pets, not cartridge games: each device has one built-in
program in mask ROM. "Support" means the emulator reproduces that device's
hardware, so supplying its ROM is the whole job — there is no per-game code.

TamaLib covers **eight devices across two MCUs** (from its README):

| MCU | Devices | Clock |
|---|---|---|
| **E0C6S46** | Tamagotchi **P1**, Tamagotchi **P2** | 32,768 Hz |
| **E0C6S48** | Tamagotchi **Angel**, **Umino** (Ocean), **Morino** (Forest), **Mothra**, **Tamaotch**, **Genjintch** | 32,768 Hz and 1 MHz |

All eight are 32×16 monochrome with 8 icons.

How much of that is actually exercised here:

- **Verified in this repository: P1 only.** The operator's ROM hashes to
  TamaTool's P1 constant (`CRC 0xC7875F27` over `0x2F0..0x400`), and it boots,
  animates and responds to buttons.
- **Supported by the core, untested here: the other seven.** No code change is
  needed — dropping in an Angel or Morino ROM should work, because the E0C6S48
  support is compiled in and `cpu.c` already switches between the 32 kHz and
  1 MHz oscillators when the ROM asks for it.
- **Device identification is only automated for three.** TamaTool, by the same
  author, has CRC constants for P1, P2 and Angel and *defaults everything else
  to P1*. So an unrecognised ROM is not proof of an unsupported one.

Both MCUs are enabled in our build (`E0C6S46_SUPPORT` and `E0C6S48_SUPPORT` in
`core/tamalib/cpu.h`). Note that `hw.c` prefers the E0C6S48 segment map because
its `#if` comes first — that is harmless for a P1, because the E0C6S46 map and
the first 40 entries of the E0C6S48 map are **byte-identical** (verified), and a
P1 ROM never addresses the extra 11 segments.

Not covered by any of this: the **Connection series** (V1–V6, which is what our
V3 core is), Tamagotchi Friends, and everything colour. Those are different
hardware entirely.

## Keys

```
A / left arrow    left button
S / down arrow    middle button
D / right arrow   right button
space             tap sensor (shake)
F1 / F2           scale down / up
Esc               quit
```

Window opens at scale 4 (656×376). Scale 14 was the initial default and was
absurdly large for a 32×16 panel.

## Two bugs found while building this

**1. Double pacing.** TamaLib paces itself: `cpu_set_speed(1)` makes
`cpu_step()` call `sleep_until()` for the real duration of each instruction.
The first version *also* paced the render loop on its own deadline, so the two
fought and the machine ran at roughly the wrong rate. Fixed by letting TamaLib
own the timing and using this loop only to decide when to draw.

**2. Register dumping looked like garbage.** `state_t` holds **pointers** to the
live registers (`u13_t *pc`, `u4_t *a`), not values. Printing the struct fields
directly gives recycled heap addresses, which look like plausible nonsense.
Dereference them.

## Attribution

TamaLib is copyright (C) 2021 Jean-Christophe Rona, GPL-2.0-or-later. See
`ATTRIBUTIONS.md`. The device documentation it is based on is linked from its
README; the E0C6S46/48 technical manual is public from Epson.
