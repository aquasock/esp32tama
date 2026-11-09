# Core validation

The C core is **bit-exact** against the BrickEmuPy Python reference, verified by
instruction-level trace comparison and full memory-state comparison.

## Method

`ref_trace.py` drives the Python reference and `corebench --trace` drives the C
core. Both emit, every N instructions:

```
instrs cycles pc a x y sp p
```

Any difference in semantics, cycle accounting, flag handling or memory access
shows up as a divergence in that stream.

For end state, both emit register file, program counter, and FNV-1a hashes over
the full 1216-byte CPU RAM and 512-byte display RAM.

## Results

| Test | Coverage | Result |
|---|---|---|
| Trace, 200 checkpoints x 5000 | 1,000,000 instructions | **identical** |
| Trace, 400 checkpoints x 5000 | 2,000,000 instructions | **identical** |
| Final state (RAM + DPRAM hashes) | 3,000,000 instructions | **identical** |
| Final state (RAM + DPRAM hashes) | 10,000,000 instructions | **identical** |

10,000,000 instructions is 1,336,067,842 emulated cycles — about **334 seconds
of emulated time**, i.e. over five minutes of the Tamagotchi running, with the
full register file and both memories matching at the end.

## Bugs found and fixed along the way

Each of these was found by bisecting a trace divergence, not by inspection.

1. **Fractional sleep ticks.** The reference returns `clock / 32768` per idle
   tick — 122.0703125 cycles, not 122. Truncating lost ~0.07 cycles per tick and
   drifted the cycle count. Cycles are now accumulated as a `double`, exactly
   like the reference.

2. **`JMP (ind)` is 6 cycles, and does not page-wrap.** The reference reads the
   high byte at `ptr+1` with no 6502 wrap bug. I had 5 cycles and the wrap.

3. **Irregular cycle counts.** The reference has entries that are not
   arch-correct — `AND zp,X` costs 6 where `ORA zp,X` costs 3; `CPY abs` costs 3
   where `CMP abs` costs 4. These are reproduced deliberately, from a table
   generated out of the reference source, because the ROM's timing was tuned
   against them.

4. **Page-crossing penalties.** 26 opcodes add +1 cycle when the effective
   address crosses a page, including `abs_x`, `abs_y` and `ind_y`. Tracked by
   setting a `page_crossed` flag in the addressing helpers.

5. **Taken branches.** 2 cycles not taken, 3 + page penalty when taken. The
   page-crossing flag for a branch is computed from `(target ^ (pc+1)) > 255`.

## Why matching the reference matters more than being correct

A strict 6502 implementation would disagree with the reference on a dozen
opcodes. The ROM was written for, and is timed against, this specific core. A
bit-exact port is the only way to know that what runs on the watch is what runs
in the emulator — and it makes any future divergence a real bug rather than a
difference of opinion about `AND zp,X`.

## Reproducing

```sh
gcc -O2 -Wall -Wextra -o corebench splb32.c corebench.c

python3 ref_trace.py ROM 200 5000 > py.trace
./corebench ROM --trace 200 5000 > c.trace
diff py.trace c.trace          # must be empty

python3 ref_trace.py ROM --final 10000000 > py.final
./corebench ROM --final --instrs 10000000 > c.final
diff py.final c.final          # must be empty
```

## Rendering validation

The segment map is generated from the SVG artwork by `gen_segmap.py`, which
parses full SVG path syntax (M/L/H/V/C/S/Q/T, absolute and relative), flattens
cubic/quadratic curves, and scanline-rasterises each segment to horizontal runs.

### The bug that made this necessary

The first generator computed each segment's bounding box from the raw numbers
in the `d` attribute, assuming they alternate x and y. They do not: the artwork
uses `H` and `V` commands (1921 and 1923 of them) that carry a single
coordinate, plus cubic curves whose control points are not vertices. Every dot
therefore got a box spanning the whole artwork, and drawing those produced a
grid of thin lines across the LCD.

### Verified against an independent renderer

Ground truth is the same display state rendered straight from the SVG by
ImageMagick/rsvg with unlit segments removed, so only segment geometry is
compared.

| Frame | IoU | Precision | Recall |
|---|---|---|---|
| Egg sprite | 0.927 | 0.955 | 0.969 |
| Clock-set screen | 0.857 | 0.986 | 0.868 |

Precision above 0.95 in both: essentially all ink drawn is in the right place.
The lower recall on the clock screen is antialiasing at dot edges, not misplaced
geometry.

### Resolution matters, and it is not a bug

Each LCD dot is 22.5 units across on a 25.1-unit pitch: the artwork carries a
2.6-unit gap, which is ~10% of the pitch. At a 288 px render that gap is
0.60 px against a 5.17 px dot -- rounding it up to one whole pixel turned a
subtle dot matrix into an obvious drawn grid.

Default is now 576 px (1.2 px gap against a 10.3 px dot), which reads correctly.
Change it with:

    SEG_RES=1152 python3 gen_segmap.py <svg> segmap.c segmap.h

### Frame animation

Display state must be sampled at an interval that is not a multiple of the
frame period. Sampling every 100000 cycles (25 ms) produced identical hashes
across 60 seconds and looked like a frozen emulator; sampling at 77777 cycles
showed six distinct states in four seconds. The display was animating the whole
time -- the measurement was aliased.

## Watch result (T-Watch Ultra, Linux 7.2.4, Xtensa LX7 @ 240 MHz)

Measured by the agent working in `Escapement/` on 2026-11-09, on a fresh boot:

```
SPLB32 C core benchmark
  emulated cycles   40000011
  instructions      454305
  avg cycles/instr  88.05
  wall time         3.770 s
  emulated cycles/s 10608970
  instructions/s    120493
  realtime factor   2.65x   (target 4,000,000 cycles/s)
```

**2.65x realtime**, and 2.33x with a per-frame renderer running alongside. Above
1.0, so the watch genuinely keeps up with real Tamagotchi hardware.

`avg cycles/instr 88.05` is the sleep batching working as designed: 454 K
instructions for 40 M emulated cycles, because the firmware is idle most of the
time.

### Rendering is correct on the watch

`tamafb` cannot write to `/dev/fb0` on that board -- `co5300.c` provides only
`fb_fillrect` / `fb_copyarea` / `fb_imageblit`, with no `fb_write` and no
`fb_mmap`, so `fbcon` works but userspace has no path to the glass. That is a
kernel driver gap, not a core problem.

To prove the renderer anyway, a PPM dump was pulled off the device and it is
correct: egg sprite, right proportions, LCD dot pattern, distinct bezel and
LCD-off colours. See `for-escapement/egg-on-watch.png`.

### The memory constraint, and why it was partly mine

The first build failed to load at all:

```
nommu: Allocation of length 1052672 from process 222 (corebench) failed
```

That 1 MB figure was **my bug**: `corebench.c` did

```c
rom = malloc(romlen + SPLB32_ROM_BYTES);
memcpy(rom, rom_embed, romlen);
```

when `splb32_new()` only stores the pointer and the core never writes to the
ROM. A second 512 KB copy for nothing. Fixed at 07:40 -- point at `rom_embed`
directly and guard the `free()`.

After that fix the load segment is ~544 KB, which still will not fit once the
kernel's largest contiguous free block has settled at 512 KB (~15 min uptime).
That is an unavoidable consequence of the 512 KB ROM being resident: it alone
fills the largest block.

**If the watch demo is pursued**, the fix is demand-bank loading -- keep the
current ROM bank and the fixed bank resident (64 KB) and read a bank from the
compressed image on SFR `$00` writes. The ROM compresses to 221 KB with zlib,
185 KB with lzma, so a bank cache plus compressed store is comfortably under any
contiguous limit. That is a real change to `read8` and not worth doing unless
the fb driver gap is also closed.

## EEPROM model and machine save state

### The EEPROM

The V3 keeps persistent state (name, age, hunger, character, generation) in a
1024-byte HT24LC08 I2C EEPROM on port D, bit-banged by the firmware. Ported from
the reference's `peripherals/HT24LC08.py` -- START/STOP detection, address byte,
word address, ACK timing, sequential read/write.

Verified equivalent to the reference:

| Check | Result |
|---|---|
| 200 instructions with EEPROM attached | identical PC, regs, flags, cycles, PD |
| 10,000,000 instructions, EEPROM hash | identical (`1F116DC5`) |
| Port-write activity count | same order of magnitude |

### But it does not give persistence

With the EEPROM attached and the firmware running, driving buttons and letting it
run five emulated minutes, **the ROM reads the EEPROM but never writes it**:

```
after boot     edges=   6  bits=    0
after buttons  edges=4829  bits= 1497
after 5 min    edges=5429  bits= 1497
eeprom writes: 0
```

1497 bits is roughly 187 bytes of traffic, all reads. The reference does exactly
the same thing, so this is faithful behaviour rather than a port defect.

That means an emulated pet does not survive a restart through the EEPROM path.
Whether real hardware writes under conditions we cannot drive (a long
age-out, a shutdown hook) is unresolved.

### So: full-machine save state

`splb32_state_save` / `splb32_state_load` snapshot the whole machine -- PC,
registers, flags, 1216 bytes of RAM, 512 of display RAM, SFRs, timers, bank,
port latches and drive state, instruction/cycle counters, and the EEPROM image.
2960 bytes, versioned, size-checked, magic-checked.

Fidelity test: run straight to 3,000,000 instructions versus stop at 1,200,000,
serialise, destroy the machine, rebuild, load, run the remaining 1,800,000.

```
straight : pc=5E7E cyc=366132721 hash=F55815A8
resumed  : pc=5E7E cyc=366132721 hash=F55815A8
PASS
```

A corrupted magic word is rejected with rc=-2 rather than silently loading
garbage.

The desktop front end uses this: state goes to `$HOME/.tamagui.state` on exit
and on F5, is loaded on start, and `--new` starts a fresh pet. F9 reloads.

## Confirmed in play (2026-11-09)

End-to-end behaviour observed by the operator on the desktop front end, after
the segment-map and pacing fixes:

- boot, egg bounce, hatch
- set the clock through the menu
- cycle all ten status icons
- feed medicine, clean up, praise — the pet becomes happy
- save on exit and resume on relaunch, continuing from the same pet

That exercises the whole chain: PA button input, the ROM's own logic and timers,
display RAM updates, the segment rasteriser, and the machine save state.

Untested by me directly: the three top-row icon segments (`1_2`, `1_3`, `6_3`)
never lit in 677 seconds of automated button-mashing, so I could not confirm
them mechanically. They render correctly in isolation and the operator reports
the icon bar behaving properly in play.

## Watch follow-up (2026-11-09, second round)

The agent in `Escapement/` closed the fbdev gap (the `co5300` driver gained
`fb_write`), so `tamafb` now paints the panel. One run, one ROM load, sweeping
configurations — because on that board only one 512 KB contiguous allocation
succeeds per boot:

```
ROM 524288 bytes
full speed / no display:
  headless        cyc/s=16200654  realtime=4.05x
with display, by frame rate:
  fps=2           cyc/s= 6666800  realtime=1.67x
  fps=4           cyc/s= 4166829  realtime=1.04x
  fps=8           cyc/s= 2416769  realtime=0.60x
  fps=16          cyc/s= 1291716  realtime=0.32x
  fps=32          cyc/s=  666700  realtime=0.17x
```

**The display is the bottleneck, not the core.** Headless is 4.05x; the cost of
painting a 320x400 frame is ~190 ms, roughly constant per frame regardless of
pacing. At 4 fps the core still runs real-time (1.04x), which is the operating
point for a Tamagotchi. Raising the frame rate starves the emulator because each
frame is ten synchronous SPI band transfers.

### Two fixes taken from their report

1. **`tamafb` rendered into a whole-frame BSS buffer.** 758,904 bytes of BSS,
   which on NOMMU is part of the load segment and fails to allocate once memory
   fragments. Rewritten to render in 40-row bands (25 KB), verified
   **pixel-identical** to the whole-frame renderer (0 differing pixels of
   128,000). BSS is now 71,328 bytes.

2. **`corebench`'s redundant ROM copy.** `malloc(romlen + SPLB32_ROM_BYTES)`
   followed by a memcpy of the embedded ROM — a second 512 KB for nothing,
   since `splb32_new()` only stores the pointer. Removed; `rom` now points
   straight at `rom_embed` and the `free()` is guarded.

### What remains, and it is inherent

Even after the band fix the program must hold 512 KB of ROM resident, and on
that board the largest contiguous free block settles at 512 KB after a few
minutes, so a load segment containing the ROM will not fit. `-Os` brings the
largest segment to 587 KB, still over.

The real fix is **demand-bank loading**: keep the current bank plus the fixed
bank resident (64 KB) and fetch a bank from a compressed image when SFR `$00`
changes. The ROM compresses to 221 KB (zlib) or 185 KB (lzma), so a bank cache
plus a compressed store fits any contiguous limit comfortably. That is a change
to `read8` and is not attempted here — the watch already answers the question
the exercise was asking.

## Display geometry, and choosing the dot size

Measured from the artwork, not estimated.

```
Tama     LCD dot matrix      32 x 30 = 960 segments
         status icons        10 segments (5 above the grid, 5 below)
         dot                 22.5 units square
         gap between dots    2.6 units
         pitch               25.1 units
         full artwork        1219.7 x 1254.6 units (essentially square)

T-Watch  fb0                 320 x 400 RGB565, 640-byte lines
         panel               410 x 502 physical, 320 x 412 usable
```

The artwork is 1220 units wide and the watch framebuffer is 320 px, so the
whole LCD has to be scaled by about 0.26. At that scale the 2.6-unit inter-dot
gap is well under a pixel and rounds up to a whole dark line, which is what made
the display look like it was covered in a grid.

### The fix: snap the dots instead of scaling them

`SEG_DOT` / `SEG_GAP` fix the scale from the dot pitch and then place every dot
as an exact rectangle on that grid. No rounding, so no aliasing at any size.
Icons keep their real shapes, scaled to match the pitch.

Verified in the generated data, not by eye:

```
dot 16_0:  row 51  x 42..46  width 4
           row 52  x 42..46  width 4
           row 53  x 42..46  width 4
           row 54  x 42..46  width 4

x positions of consecutive dots: 42, 47, 52, 57, 62, 67, 72, 77
```

Exactly 4 px wide on a 5 px pitch.

### Fitting table

| dot | gap | output | margin vs 320x400 | fits watch |
|---|---|---|---|---|
| 4 | 1 | 243 x 250 | 77 x 150 | yes |
| **5** | **1** | **292 x 300** | **28 x 100** | **yes — default** |
| 6 | 1 | 340 x 350 | -20 x 50 | no, desktop only |

5 is the largest pixel-exact size that fits. 6 overshoots the width by 20 px
because the icon rows extend past the 32-dot grid.

**4, 5 and 6 all render correctly.** The choice is preference, not quality.
Larger dots read more easily; the 1 px gap stays crisp at every size.
