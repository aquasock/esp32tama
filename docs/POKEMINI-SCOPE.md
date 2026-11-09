# Pokémon Mini — scope

Second platform alongside the Tamagotchi Connection V3. Decided 2026-11-09.

## Why this one

It is the only remaining candidate that is genuinely unported to this class of
hardware *and* has games people want to play. The Tama-Go was dropped because a
public emulator already exists, its playable path needs a figure cartridge that
is not in the dump, and the device itself has a poor reputation with users.

## Hardware

| | |
|---|---|
| CPU | Seiko Epson **S1C88** ("Minx"), 8-bit with 16-bit ops |
| Clock | **4.00 MHz**, 4-cycle instruction fetch → **~1 MIPS** |
| RAM | 4 KB (+ 256 B I/O), 8 KB region mapped |
| ROM | up to 2 MB cartridge, 24-bit address bus (21 bits mapped) |
| Display | **96×64 monochrome** LCD, 72 Hz, driven by a separate PRC chip |
| Input | D-pad, A, B, C, Power, plus a shock/rumble sensor |
| Sound | 3-channel PWM + noise |

The display is not a plain framebuffer: the **PRC (Program Rendering Chip)**
owns it, and commercial ROMs talk to the PRC rather than the panel. That
insulates us from the panel details but means the PRC must be modelled.

## Prior art — and why it changes the plan

**PokeMini** by JustBurn (github.com/pokemon-mini/PokeMini), version 0.61.

- **Licence: GPL-3.0** — the same licence this project already uses.
- Mature: fifteen years of development, and it is the reference implementation
  everyone uses.
- Ships a **complete instruction-set reference** in `doc/` (one HTML page per
  opcode, plus the CPU, memory, IRQ/BIOS, timers, I/O, PRC, LCD and cartridge
  documents). No reverse-engineering needed.
- Ships a **free BIOS** (`freebios/freebios.min`, 4 KB, GPL) so the core can be
  exercised without any commercial ROM.
- Already ported to far weaker hardware than the watch: **NDS (67 MHz ARM9)**,
  Dreamcast (200 MHz SH-4), PSP (333 MHz MIPS), Dingoo (360 MHz MIPS), Wii,
  GameCube.

So the plan is **not** "write an S1C88 core". It is "port PokeMini's core to the
T-Watch and wire it to the V3's existing front end". That removes the largest
and riskiest piece of work, and the NDS port is the existence proof for the
performance question.

## Performance — measured, then estimated

Measured on this PC, with the free BIOS running, counting CPU steps:

```
5,000,000 emulated cycles in 0.0092 s
  = 68,097,706 CPU steps/sec
  = 14.7 ns per emulated instruction
```

That is roughly 44 host cycles per emulated instruction on x86. The figure is
the *core interpreter* cost including the per-cycle timer, PRC and audio sync.

Extrapolating to the T-Watch Ultra (ESP32-S3, 240 MHz):

- Requirement: **1 MIPS** sustained (4 MHz ÷ 4 cycles).
- If the ESP32-S3 costs 150–200 host cycles per emulated instruction — a
  reasonable range for a cacheless-ish Xtensa core running this interpreter —
  that gives **1.2–1.6 MIPS**, i.e. 1.2–1.6× realtime.
- Frame budget at 72 Hz is 4 MHz ÷ 72 = **55,555 emulated cycles**, about
  13,900 instructions per frame.

A dedicated gate report on the display path is in
`docs/POKEMINI-DISPLAY-GATE.md`: it passes conditionally, with 61-98 fps
available at 3x zoom using dirty rectangles on the current driver, and far more
with DMA.

**This is an estimate, not a measurement.** The decisive evidence is that
PokeMini runs at playable speed on a **67 MHz ARM9**, which is a quarter of the
ESP32-S3's clock and a weaker core. That makes full speed very likely, but the
only honest confirmation is to build it and measure on the watch — exactly the
mistake made with the Tama-Go, where a plausible model was built for a week
before checking whether prior art existed.

## The real gate is the display path, not the CPU

`CANDIDATES.md` records a measured property of this watch: painting a 320x400
frame through the CO5300 SPI driver costs **~190 ms**. That is 1.48 us per
pixel, and it dominates everything else.

Pokemon Mini is 96x64 = 6,144 pixels, so the cost scales with the square of the
zoom factor:

| Zoom | Pixels | Est. paint time | Ceiling |
|---|---|---|---|
| 1x (96x64) | 6,144 | ~9 ms | ~110 fps |
| 2x (192x128) | 24,576 | ~36 ms | ~27 fps |
| 3x (288x192) | 55,296 | ~82 ms | ~12 fps |

At native 1x the CPU budget is comfortable and the display is not a problem —
but 96x64 on a 410x502 panel is a postage stamp. At 3x it looks right and the
display caps the frame rate at ~12 fps.

**Two things could change this, and both must be measured before committing:**

1. **Dirty rectangles.** The 190 ms figure is for a full-screen update. Pokemon
   Mini games rarely change the whole panel; a status bar and a couple of
   sprites is typical. If only changed rows are pushed, the effective rate can
   rise several-fold. The V3 front end already does dirty tracking, so the
   machinery exists.
2. **The driver itself.** 1.48 us per pixel is about 1.35 MB/s at 16 bpp, which
   is far below what the SPI link should carry. That suggests per-band overhead
   rather than a hardware limit, and it may be fixable with DMA or larger
   transfers. This has not been investigated.

So the honest statement is: **CPU is probably fine, display is the risk, and the
risk is measurable in a day of work before any port happens.**

## Plan

1. **Vendor PokeMini's core** under GPL-3.0 with full attribution. Keep it as a
   separate `core/pokemini/` tree so the V3 core stays untouched and remains the
   flagship.
2. **Build a headless harness first**, before any UI: run the free BIOS, then a
   homebrew ROM, and measure realtime factor on the host.
3. **Cross-compile for the ESP32-S3 and measure on the watch.** This is the
   go/no-go gate. If it lands below ~1.5× realtime there is no headroom for the
   display path and the platform does not ship.
5. Only then wire it to the front end: reuse the V3's window, keyboard and
   settings plumbing. Display is 96×64 mono, which scales cleanly to the watch's
   320×380 panel at 3× (288×192) with integer pixels.
6. Audio last, as with the V3.

## Rules carried over

- **No ROM is ever distributed.** The free BIOS is GPL and ships; commercial
  `.min` files do not. Same policy as the V3.
- GPL-3.0 for the whole repository; PokeMini's authorship goes in
  `ATTRIBUTIONS.md`.
- The V3 core is not to be disturbed. It is finished and verified.

## Open questions

- PRC accuracy: how much of the rendering chip has to be emulated for the
  common games to look right. PokeMini's implementation is the answer, but its
  scope needs reading.
- Whether the shock sensor has a sensible mapping on a watch (it is a rumble
  device; the watch has no rumble, so a screen-tap gesture is the natural
  stand-in).
- Audio: 3-channel PWM at 72 Hz frame granularity may be too coarse; needs the
  same treatment the V3 sound engine got.
