# Provenance

Where the code and the data come from, and what is deliberately absent.

## Code

| Component | Origin |
|---|---|
| `core/splb32.*` | C port of BrickEmuPy `cores/SPLB32.py` (CC0) |
| `core/ht24lc08.*` | C port of BrickEmuPy `peripherals/HT24LC08.py` (CC0) |
| `core/con_v3_ir.*` | C port of BrickEmuPy `peripherals/CON_V3_IR.py` (CC0) |
| `core/splb32_sound.*` | C port of BrickEmuPy `cores/SPLB32sound.py` (CC0) |
| `frontends/segmap.c` | Rasterised from `assets/Tamagotchi_v3.svg` (CC0) |
| `frontends/*.c` | Original |
| `tools/*` | Original |

CC0 imposes no conditions, so this repository is GPL-3.0 as a whole. See
`ATTRIBUTIONS.md`.

## The ROM

**Not included.** The 512 KB firmware image is Bandai's copyrighted work.

The emulator needs it. Without it the code builds and the tools run but there is
nothing to execute. Users supply their own; dumps circulate on archive.org.

## What is verified, and against what

Every "bit-exact" claim in `VALIDATION.md` means a differential test against
BrickEmuPy produced identical PC, registers, flags, cycle counts and,
where applicable, memory hashes. The reference is the arbiter — not a
specification and not real hardware.

That matters, because BrickEmuPy differs from a correct 6502 in a dozen places.
Three of those are outright bugs, reproduced here on purpose (`BUGS.md`). Where
this project and a datasheet would disagree, this project follows BrickEmuPy,
because the firmware was written for the hardware and timed against that
implementation.

## What is not verified

- **Real hardware.** No emulated output has been compared against a physical
  Connection V3. The ROM is the only oracle.
- **Two devices connecting.** The infrared bearer is verified byte for byte; a
  full connection handshake between two emulated instances has not been driven.
- **Audio timbre.** Event counts and amplitudes match; the waveform is not
  modelled by either implementation.
