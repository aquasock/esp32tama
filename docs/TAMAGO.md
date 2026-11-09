# Tama-Go and Friends — feasibility

Answering "can we emulate the newer Tamas Natalie worked on?"

## Tama-Go (2010) — yes, and it is a genuinely good target

### The chip is documented

Natalie's repository contains the **actual GeneralPlus datasheet** for the part:

`die/GPLB52640A-52320A-51640A-51320AV21_ds.pdf`, 28 pages, version 2.1, July 2012.

Read from it directly:

```
CPU          8-bit CMOS microprocessor
RAM          1536 bytes
ROM          GPLB52320A  320 KB
             GPLB52640A  640 KB
             (GPLB51640A 640 KB, GPLB51320A 320 KB)
Clock        FCPU = 6.0 MHz @ 5.0 V typical, 8.0 MHz max
             programmable /2, /4, /8, /16, /32, /64, /128
Timers       two 16-bit auto-reload
Display      64 segments x 32 commons = 2048 dots, mono or 4 grey levels
             dedicated 512 bytes display RAM
Audio        8-channel SPU, 12-bit DAC, 10-bit PWM
I/O          16 general purpose, SPI
```

This matters more than it sounds. Every other candidate so far has been
reverse-engineered from behaviour. This one has a **manufacturer datasheet**,
which means register maps and timing come from the vendor rather than from
bisecting a reference emulator.

### The ROM is dumped and downloadable

Her `codedump/rompages/` holds 20 files totalling 1,453 KB. I reconstructed a
640 KB image from them earlier in this project (see
`Tamagotchi_TamaGo_reconstructed.bin`): each file is a 64 KB capture of the CPU
address space with the 32 KB bank at offset 0x4000, and the reset vector reads
0xCC00, the documented Generalplus user-program start.

Note a discrepancy worth resolving: she identifies the part as **GPLB52320A**
(320 KB) but the dump is 640 KB, which is the **GPLB52640A** size. Either the
board carries the larger part or the capture includes both banks. Not resolved.

### No emulator exists

This is the point. Searched: BrickEmuPy's 23 cores (its `GPL191X` is a different
Generalplus part — 512 B RAM, 128 B display RAM, not GPLB), MAME, and the
Tamagotchi hacking community. **Nothing emulates the GPLB52320A.**

BrickEmuPy's `TamagotchiMini2017` runs on `GPL191X`, a different chip at
1.5 MHz. Not a starting point for this.

### Feasibility on the watch, by the numbers

| | SPLB32 (done) | GPLB52320A |
|---|---|---|
| CPU | 8-bit, 6502-family | 8-bit Generalplus (unknown ISA) |
| Clock | 4.0 MHz | 6.0 MHz typical |
| ROM | 512 KB | 320 KB (or 640 KB) |
| RAM | 1,216 B | 1,536 B |
| Display dots | 960 | 2,048 |
| Datasheet | no | **yes** |

Measured: the SPLB32 runs at **2.65x realtime** on the watch. Scaling by clock,
6/4 gives **about 1.8x realtime** — still above real time, but the margin is
thinner.

Two things push the other way:

- **The ROM is smaller** — 320 KB against 512 KB. The contiguous-allocation wall
  that limits the watch build is *less* of a problem here, not more.
- **The display is bigger** — 2,048 dots against 960, so roughly double the
  segment count. Given the display is already the watch's bottleneck, doubling
  it is the real cost. It would need the same dot-snapping treatment.

### The honest unknown

**The CPU architecture is not described.** The datasheet documents the
peripherals, memory and display controller in detail, but the instruction set is
in a separate programming manual that is not public. So a port would still need
either that manual or a disassembly effort.

That is the whole risk of this target and it should not be glossed over: a
datasheet is not an ISA reference, and the Generalplus core is not a 6502.

## Tamagotchi Friends (2014) — no

- **Chip publicly unidentified.** The community spec index documents the
  Tama-Go's GPLB52320A but says nothing about the Friends' part.
- **ROM was never fully dumped.** Natalie's own words on the Friends: *"Pages 22
  on were all 0xFF (disclaimer: I didn't actually dump after the first few)."*
- **No emulator** for it anywhere.
- Its distinguishing feature is **NFC**, which Natalie's work attacked with a
  Proxmark3 — not a starting point for emulation.

Not viable without first solving an unknown chip and an absent ROM.

## What this means for positioning

The operator's instinct is sound: the V3 port is novel *because* it is first,
and folding fifteen earlier Tamagotchis into the same binary would dilute that.

The Tama-Go is the opposite shape of opportunity — **not** a rehash of something
everyone has done, but a documented, dumped, genuinely unemulated device. It
extends the project forward in time rather than backward, and it keeps the V3 as
its own flagship.

### Recommended order

1. **Keep the V3 core as its own thing.** It works, it is bit-exact, it is
   first. Do not add cores to that binary.
2. **Tama-Go as a separate investigation**: obtain the Generalplus programming
   manual (or establish the ISA by disassembly) before committing to a port.
   That first step is research, not engineering, and it decides whether this is
   feasible at all.
3. **Pokémon Mini** remains the fallback with a free BIOS and a known-quantity
   port path.

## Sources

- GPLB datasheet: `natashenka/Tamagotchi-Hack`, `die/` — read directly, 28 pages
- Tama-Go ROM: `natashenka/Tamagotchi-Hack`, `codedump/rompages/` — 20 files, 1,453 KB
- Chip identification: `loociano/tamagotchi-tech-specs`, confirmed against the datasheet
- Friends ROM status: `natashenka.ca/tamagotchi-friends-eeprom/`, quoted above
