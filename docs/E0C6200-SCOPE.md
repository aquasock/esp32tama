# E0C6200 port — scope

Decision record for porting BrickEmuPy's Epson E0C6200 core to C, alongside the
existing SPLB32 port. Written before the work, so the estimates can be checked
against what actually happened.

## Why E0C6200 and not the SPLB32 we already have

Measured from BrickEmuPy's 69 `.brick` device configs:

```
core        devices   max ROM   clock range
E0C6200      18       12K       32k – 2.0 MHz
T7741        12        8K       170 – 210 kHz
HT943         9        4K       256k – 1.0 MHz
SPLB20        6       32K       360k – 1.1 MHz
SPLB32        2      512K       4.0 MHz     <- what we have
```

**E0C6200 is 9x the device count of the SPLB32** and holds every famous one:
Tamagotchi P1, P1J, P2, Angel, Morino, Mothra, Umino, Genjintch, Yasashii, and
Digimon V1–V4 across three regions.

It is also **far better suited to the target**:

| | SPLB32 (done) | E0C6200 |
|---|---|---|
| ROM | 512 KB | 12 KB max, 8 KB typical |
| Clock | 4.0 MHz | 1.6 MHz for the P1 |
| Contiguous memory needed | 512 KB — the watch's hard wall | 12 KB — trivial |
| Expected realtime on the watch | 2.65x measured | ~6-8x extrapolated |

The 512 KB ROM is why the watch build could only start once per boot. That
problem does not exist here.

## Scale of the work

| | SPLB32 (done) | E0C6200 |
|---|---|---|
| Python source | 2,196 lines | 2,049 lines |
| Opcode table entries | 256 | 108 |
| Distinct instruction implementations | 151 | 98 |
| RAM | 1,216 B | 768 B (`RAM_SIZE 0x300`) |
| Display RAM | 512 B | 160 B (`VRAM_SIZE 0xA0`) |
| Dispatch | flat 256-entry table | flat 108-entry table |

**Comparable, slightly smaller.** Dispatch is a flat table in both, which is the
case that ported well last time: the SPLB32 port was mechanical once the table
and the addressing modes were extracted.

## Differences that will cost time

The two cores are not shaped alike, and these are the parts that will not carry
over mechanically.

### 1. A 4-bit CPU, not 8-bit

The E0C6200 is a 4-bit machine: a 4-bit ALU, 4-bit registers, and memory
organised as 4-bit nibbles. `RAM_SIZE 0x300` is 768 *nibbles*, not bytes.
Every load, store and arithmetic op needs a nibble-sized type discipline, and a
naive `uint8_t` port will be wrong in ways that only show up in the flags.

### 2. Ten ports, not four

`K0, K1, R0-R4, P0-P3`, each with its own direction register, pull-up mask,
push-pull mask and latch. The SPLB32 had four and that was already the fiddliest
part (the PB direction expansion bug cost an hour). Budget for the same again.

### 3. LCD is a separate mechanism

The SPLB32 had DPRAM the front end could read directly. Here the display is
160 nibbles at `0xE00` and `0xE80`, and the segment-to-pixel mapping comes from
the `.brick` asset rather than a single SVG. **The segment map generator will
need a second input path.** This is the largest genuinely new piece of work.

### 4. Timers are four, not two

TM (8-bit timer), SWL/SWH (100 Hz stopwatch), PT (programmable timer with 7
divisor options), and the OSC1 clock divider. The SPLB32's two timers plus
1 kHz timebase do not map onto these.

## What carries over unchanged

This is the part that makes it tractable:

- **The differential test method.** `ref_trace.py` and `optest.py` are already
  reference-agnostic in shape — they instantiate a core, step it, and compare
  state. They need a new core binding, not a new design.
- **`fuzz_diff.py`** — same, with a different opcode table.
- **The discipline that found twelve bugs.** Especially the negative control: an
  injected bug must make the tests fail. That is not optional; it is what caught
  the opcode test passing 151 opcodes while executing nothing.
- **The front ends.** `tamagui.c` is thin: buttons in, DPRAM out, render. If the
  C core exposes the same shape, the front end needs only a new segment map.
- **`gen_segmap.py`** — the path parser and rasteriser are reusable; only the
  input format changes.

## Proposed structure

A second core in the same repository, not a second repository:

```
core/       splb32.c  splb32.h  ...     (existing)
            e0c6200.c e0c6200.h         (new)
            common/                     shared: nibble helpers, port model
frontends/  unchanged; picks a core at build time
tools/      ref_trace.py gains a --core flag
docs/       E0C6200 notes alongside the SPLB32 ones
```

`attiny`-style duplication of the front end is the thing to avoid. One renderer,
one input path, two cores behind a small vtable.

## Order of work

1. **Memory and registers**, no instructions. Verify against the reference by
   comparing post-reset state.
2. **Opcode table**, mechanically extracted from the Python rather than typed.
   Ideally generated, as the SPLB32 cycle table was.
3. **Differential trace at increasing lengths**, bisecting every divergence.
   Expect to find reference bugs; reproduce them.
4. **Ports and timers** — drive a real ROM to a stable display.
5. **Segment map** for a `.brick` asset; requires the new input path.
6. **Front end hookup** and the same end-to-end test: does a P1 hatch.

## Honest risks

- **4-bit arithmetic is genuinely different.** More likely to produce subtle
  flag bugs than the 8-bit port was.
- **No ROM is shipped.** The 18 devices are marked "distributed without ROM", so
  testing needs a P1 dump — which is findable, but not included.
- **The reference has bugs.** The SPLB32 port found three. Expect the same here,
  and expect to reproduce rather than fix them.
- **This is not a weekend.** The SPLB32 port took a full session with a working
  test harness already in place. The E0C6200 adds a new display path and a new
  arithmetic width; call it 1.5-2x the effort.

## Measurement

The claim at the top — "6-8x realtime on the watch" — is an extrapolation from
clock speed and working-set size, not a measurement. The way to check it is the
same as last time: get it running headless on the host, then cross-compile and
ask for one benchmark run on the watch.


---

# Progress log

## Step 1 — machine layer: DONE

`core/e0c6200.c` implements the nibble-addressed memory map, the register file,
the eleven-port model (direction, latch, pull-up, push-pull per port) and reset.

Verified against BrickEmuPy's post-reset state — every field matches:

```
          mine          reference
pc        100           100
regs      0 0 0 0 0     0 0 0 0 0
flags     0 0 0 0       0 0 0 0
R0-R4     dir=F          dir=F
P0-P3     pullup=F pushpull=F   pullup=F pushpull=F
K0,K1     dir=0          dir=0
```

The per-port asymmetry matters: R0-R4 come up as inputs, P0-P3 as outputs with
pull-ups. Getting that wrong changes what the firmware reads on its first I/O
access.

## Step 2 — instruction table: NOT DONE

The dispatch is a **4096-entry table** indexed by a 12-bit opcode (instructions
are fetched as 16-bit big-endian words with the top nibble clear), collapsing to
**98 distinct implementations** across those 4096 slots. The SPLB32's table was
256 slots / 151 implementations, so this is a different shape: more slots, fewer
unique handlers, and many handlers referenced 256 times each as ranges
(`_jp_s` occupies 256 consecutive entries).

That range structure is the thing to exploit — the table can be generated as a
compact range map rather than a literal 4096-entry array.

## What is still required

- 98 instruction handlers, in 4-bit arithmetic
- I/O register decode at `0xF00-0xF7E` (currently returns 0)
- Interrupt dispatch (six sources, vectors 0x2-0xC)
- Four timers: TM, SWL/SWH stopwatch, PT programmable, OSC1 divider
- Buzzer/envelope sound
- A segment map derived from a `.brick` asset — new input path for
  `gen_segmap.py`

## No ROM available

No P1/P2 ROM is present locally, so nothing beyond the machine layer can be
verified yet. The instruction port can be written against the reference by
synthetic ROM (as `optest.py` does for the SPLB32), but end-to-end testing needs
a real dump.
