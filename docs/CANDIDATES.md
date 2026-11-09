# Candidate hardware survey

What could plausibly run on a **T-Watch Ultra** (ESP32-S3, Xtensa LX7 @ 240 MHz,
512 KB SRAM + 8 MB quad PSRAM, 16 MB flash, CO5300 410x502 AMOLED) running
Linux, or bare-metal on the same board.

Every entry below was checked against source or vendor documentation, not
recalled. Numbers are annotated with where they came from.

## The constraint that decides most of these

Two measurements from this project and from the Escapement work:

1. **The SPLB32 at 4 MHz runs at 2.65x realtime** on the watch, headless.
2. **Painting a 320x400 frame takes ~190 ms** through the CO5300 SPI driver —
   roughly 19 ms per 40-row band. That is a display-path limit, not an
   emulation limit, and it caps the achievable frame rate near 5 fps for a
   full-screen update.

So a candidate has two independent costs: CPU cycles per emulated second, and
bytes pushed to the panel per frame.

## Summary

| Platform | CPU | Clock | Display | ROM source | Verdict |
|---|---|---|---|---|---|
| Sunplus SPLB32 | 8-bit, 6502-family | 4 MHz | 32x30 dots | proprietary | **done**, 2.65x measured |
| Epson E0C6200 | 4-bit | 1.6 MHz | 32x16 | proprietary | best next step |
| Sharp SM510 | 4-bit | 32 kHz | segments | proprietary | 59 G&W titles, real port |
| Holtek HT4BIT/HT943 | 4-bit | 1 MHz | 32x16 | 7 of 9 present | cheapest win |
| Toshiba T7741 | 4-bit | 210 kHz | 32x16 | 11 of 12 present | trivially fast |
| Sunplus SPLB20 | 8-bit | 1.1 MHz | 32x16 | 5 of 6 present | free, subset of SPLB32 |
| Pokémon Mini | S1C88 8-bit | 4 MHz | 96x64 mono | **free BIOS** | strong candidate |
| NES (retro-go) | 6502 | 1.79 MHz | 320x240 | user-supplied | **already 60 fps on Nano** |
| STM32H7 (2021 G&W) | Cortex-M7 | 280 MHz | 320x240 | proprietary | no — see below |
| MC68000 (TiEmu) | 68k | 12 MHz | 160x100 | proprietary | no — see below |

## Rejected, with reasons

### The 2021 Game & Watch (STM32H7)

Emulating a 280 MHz Cortex-M7 on a 240 MHz Xtensa to run an NES emulator is
strictly worse than running the NES emulator directly. Verified: the firmware's
vector table puts flash at `0x08000000` and RAM at `0x20000000` — an STM32, and
the game data is in a custom container, not iNES (searched the decrypted
external flash for `NES\x1a`: absent).

Also: retro-go already replaces that firmware and runs 8 platforms natively.
There is no reason to emulate Nintendo's emulator.

### TiEmu / TI-89

`Tiny89/docs/tiemu-ae350-port.md` records it running real-time on a **750 MHz
AE350 RISC-V soft core** on the Tang Console. The ELF is 4.6 MB of text. The
watch is 3x slower with no cache-coherent fast memory, and the M68K is a much
harder core than anything here (variable-length instructions, bus-error
addressing modes). Out.

### SNES

Per the operator: retro-go's SNES core cannot run even non-SuperFX titles at
full speed on this class of hardware. Recorded as given, not re-measured.

## The display problem, stated plainly

The T-Watch cannot do 60 fps for anything that fills the screen. That is a
driver property, measured. It makes the watch well suited to:

- devices whose natural frame rate is low (Tamagotchi: ~32 Hz segment refresh)
- devices with tiny displays (Pokémon Mini: 96x64 = 6 KB a frame)

and badly suited to consoles expecting 60 fps at 320x240 or larger.

**The Arduino Nano ESP32 already does NES at 60 fps** — `FPS:60 (S:30 R:30)`,
`BUSY:27%`, 8.1 MB PSRAM free, measured on the operator's TinyRetro port. Same
SoC, same clock, and **octal** PSRAM against the watch's quad. The watch cannot
beat it; it has the worse memory bus and the worse display path.

## Pokémon Mini — the strongest untried candidate

Checked against `libretro/PokeMini` source (GPL-3.0, last pushed 2026-07-31).

```
CPU        Seiko Epson S1C88 @ 4,000,000 Hz     (confirmed: MinxAudio.c,
                                                 MinxTimers.c use 4000000)
RAM        4 KB
Display    96 x 64 monochrome                   (confirmed: MinxLCD.h)
ROM        up to 2 MB, masked to a power of two (confirmed: PokeMini.c)
```

**Same clock as the SPLB32**, which we measured at 2.65x. But the display is
**14x smaller**: 6,144 pixels against our 87,600. At the measured ~190 ms per
320x400 frame, a 96x64 frame should cost roughly 10 ms — the display stops being
the bottleneck.

Core size:

```
MinxCPU_XX.c  1059    MinxLCD.c      608
MinxCPU_CE.c  1127    PokeMini.c     518
MinxCPU_CF.c   779    MinxIO.c       401
MinxCPU_SP.c   244    MinxCPU.c      113
```

About 4,300 lines. **It is a direct port, not a reimplementation** — compile the
core for the target and write a platform layer. No differential reference needed,
because the emulator already is the reference.

Delete `Video_x2.c` through `Video_x7.c` (8,000 lines of PC software scalers);
scaling 96x64 up is trivial.

**The decisive advantage: PokeMini ships a free BIOS**
(`freebios/freebios.min`, 4096 bytes, GPL-3.0, by the emulator's author). No
proprietary dump is needed to boot, and the homebrew scene supplies freely
licensed ROMs. That would make it the first emulator in this family that is
**usable and legal out of the box** — unlike the SPLB32, which needs a Bandai ROM
that cannot be distributed.

## Still to survey

Not yet assessed: WonderSwan, Neo Geo Pocket, Atari Lynx, Vectrex, Gamate,
Supervision, Mega Duck, GameKing, Casio PV-1000/PV-2000, Coleco, Intellivision.
Also unassessed from BrickEmuPy's own inventory: D750X, EM73000, GPL191X,
KS56CX2X, LC5732, M37520, STK55C324, T6770S, SPL02/SPL03/SPL61X/SPL81408.
