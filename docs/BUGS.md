# Bug hunting

The core passes the ROM bit-exactly, but the ROM only exercises **78 of 256
opcodes**. The core implements **151**. So 73 opcodes, plus nearly every edge
case, had never been executed before this work.

Three test instruments were built to close that gap. Each found real bugs, and
one of them was itself broken in a way that made it silently pass everything.

## Instrument 1 — per-opcode differential test

`optest.py` builds a tiny ROM for each of the 151 implemented opcodes:

```
LDX #$FF      ; stack pointer to a known place
TXS
LDA #$55      ; known accumulator
LDY #$10      ; known index
LDX #$08      ; known index
<opcode + operands>
NOP NOP
```

Runs it through the Python reference and the C core, then compares program
counter, A, X, Y, SP, status flags **and accumulated cycle count**.

### The harness was wrong, twice

First version put the program at flat ROM offset `0xC800`. But the CPU's
`$C800` maps to flat **`0x4800`** — `read8` does `addr & 0x7FFF` for the fixed
bank. The tests were executing zeros, which decode as `BRK`, then `BRK` again
from the vector, forever.

**151 opcodes "passed" while running nothing.**

The thing that caught it: injecting a deliberate bug (changing `AND zp,X` from 6
cycles to 4) and watching the test still pass. A test that cannot fail is not a
test. A negative control is not optional.

Every opcode test now runs against both a known-good and a known-bad binary.

## Instrument 2 — random program differential fuzzer

`fuzz_diff.py` generates programs from the 151 valid opcodes with *correct
operand lengths* (random bytes mostly land on illegal opcodes and never reach
the interesting paths). Runs both cores, compares registers and cycles.

## Instrument 3 — sanitizers

ASan + UBSan over 20,000,000 ROM instructions. Clean: no out-of-bounds, no UB,
no uninitialised reads. (The only reports were leaks in the throwaway harness
itself, not the core.)

## Bugs found

### 1. `ROR zp,X` advances PC by 2 — a reference bug, reproduced

The reference's `_ror_zp_x` does `self._PC += 2` where every other `zp,X`
instruction advances by 1. The flags were computed correctly; only the PC
advance was wrong, so execution resumes one byte past the operand.

This is a bug in BrickEmuPy, not in the hardware. It is reproduced deliberately
because the core must match the implementation the ROM was timed against.

I audited **all 151 opcodes** for this class of anomaly mechanically — parsing
each reference function for its `_PC += n` and comparing against the expected
operand length for that addressing mode. `_ror_zp_x` is the only one.

### 2. SFR `$0C` has a read/write asymmetry

```
getter  _get_sfr_timer_timebase_sel  returns  self._T1KHZ   (1 kHz counter)
setter  _set_sfr_timer_timebase_sel  writes   self._TIME_BASE
```

Reading `$0C` returns a counter, not what you wrote to it. Another reference
bug, reproduced.

Found by auditing every SFR getter/setter pair for asymmetric backing storage.
Three others are asymmetric but benign — `$01` (getter reads `_IREQ`, setter
writes `_INT_CTRL` but also clears `_IREQ`, which is a legitimate interrupt
latch) and `$04` (getter returns `_CPU_DIV`, setter also sets `_CPU_ENBL`).

### 3. ROR zero flag — all four addressing modes

The reference computes ROR's zero flag from the OLD value:

```python
self._ZF = not(prev_value & 0xFE | self._CF)
```

That is the input's upper seven bits plus the incoming carry, not the rotated
result. It is wrong — the real 6502 sets Z from the result — but it is what the
ROM was timed against, so it is reproduced. Found by random programs rather
than the per-opcode sweep, because it needs a specific value/carry combination.

### 4. `port_read` used the latch instead of the port logic

The reference's `_port_read` is:

```python
(~PDIR & (in_hi | (pullup_ext & ~in_lo))) |
(PDIR  & PLATCH & (pullup_ext | ~ATTR))
```

A port configured as **input** reads its external drive state, not what was
last written to the data register. My core returned the raw latch, so
write-then-read of a data register disagreed whenever `PDIR` had those bits
clear. Only reachable by random programs that write ports and read them back.

### 5. `RTS` does not mask the program counter

The reference's `_rts` ends with:

```python
self._PC += 1
```

No `& 0xFFFF`. Every other instruction masks, so PC normally stays 16-bit — but
an RTS from a wrecked stack can push it to `0x10000`. `_get_rom_addr` still
reports a low address (`0x10000 & 0x7FFF == 0`) while `_read_mem` takes the
`addr >= 0xC000` branch and fetches from ROM offset 0. So the CPU executes one
instruction from ROM while claiming to be at address 0.

My C core used `uint16_t pc`, which wrapped to 0 and then read the SFR bank —
a different instruction. Fixed by widening PC to 32 bits and leaving RTS as the
only unmasked increment.

This is deep in degenerate territory: it needs a random program to wreck the
stack first. The real ROM keeps a balanced stack and never reaches it. Worth
finding anyway, because "bit-exact" has to mean bit-exact.

### 6. `flat_pc` did not OR the bank into low addresses

The reference's `_get_rom_addr` is:

```python
if addr >= 0xC000: return addr & 0x7FFF
return (addr & 0x7FFF) | self._ROM_BANK
```

There is no special case for low addresses. My C version returned `addr`
unchanged below `$4000`, which only differs when a program executes from SFR or
RAM space — exactly what a stack-wrecking random program does.

## Earlier bugs, found by bisecting ROM trace divergences

Recorded in `VALIDATION.md`, summarised here:

- **Fractional sleep ticks.** Reference returns `clock / 32768` = 122.0703125,
  not 122. Truncation drifted the cycle count.
- **`JMP (ind)`** is 6 cycles and does *not* page-wrap.
- **Irregular cycle counts** — `AND zp,X` costs 6 where `ORA zp,X` costs 3;
  `CPY abs` costs 3 where `CMP abs` costs 4. Reproduced from a generated table.
- **Page-cross penalties** on 26 opcodes, including `abs_x`, `abs_y`, `ind_y`.
- **Taken branches** — 3 + page penalty, not 3 flat.

## Result

| Test | Result |
|---|---|
| 151 opcodes, targeted, registers + flags + cycles | 0 mismatches |
| 400 random programs, seed 11 | 0 divergences |
| ROM trace, 2,000,000 instructions | identical |
| ROM final state, 10,000,000 instructions | identical |
| ASan + UBSan, 20,000,000 ROM instructions | clean |

## What this says about the reference

BrickEmuPy is not a clean 6502. It has at least two outright bugs (`ROR zp,X`
PC advance, SFR `$0C` getter) and a dozen cycle counts that disagree with real
hardware. The ROM still runs correctly under it, which means either the bugs are
never reached by the real firmware, or the firmware tolerates them.

Either way: **a correct 6502 core would not run this ROM identically.** Matching
the reference was the right call.

## Reproducing

```sh
gcc -O2 -I. -o /tmp/fuzzcore fuzzcore.c splb32.c
python3 optest.py                 # must print 0 mismatches
python3 fuzz_diff.py 400 --seed 11
```

---

# Rendering layer

These are in the display path, not the CPU core, so they do not affect
bit-exactness. They affect what you see.

## 7. Segment bounding boxes from raw path numbers

`gen_segmap.py` originally took the numbers in each segment's SVG `d` attribute
and assumed they alternated x, y. The artwork uses `H` and `V` commands (1921
and 1923 occurrences) that carry a single coordinate, and cubic `C` curves whose
control points are not on the shape. Every one of the 960 dots got a bounding
box spanning most of the artwork, and drawing those filled the LCD with a grid
of thin lines.

Fixed by writing a real path parser and scanline rasteriser. Validated against
an independent rsvg render at IoU 0.93.

## 8. Inter-dot gap resolved too coarsely (renderer artefact, not bad geometry)

Each dot is 22.5 units on a 25.1-unit pitch. At a 288 px render the 2.6-unit gap
is 0.60 px against a 5.17 px dot, so rounding made the physical dot matrix look
like a drawn grid. Default moved to 576 px. This is a sampling artefact of the
renderer, not incorrect geometry -- the artwork genuinely has gaps between dots,
and so does the real LCD.

## 9. Aliased frame sampling (my measurement, not the emulator)

Sampling display RAM at exactly 100000 cycles looked static over 60 emulated
seconds. The frame period divided cleanly into the sample interval, so every
sample landed on the same phase. Sampling at 77777 cycles showed six distinct
states in four seconds.

The emulator was animating correctly the whole time. Recorded here because it is
the third time in this project that a measurement artefact looked exactly like a
feature being absent.

## 10. Three segments were not `<path>` elements

`gen_segmap.py` matched `id="byte_bit"` only on `<path>` elements. Three of the
970 segments are `<g>` groups holding their whole artwork:

```
1_3   976 paths   -> game icon (bat and ball)
1_2   972 paths   -> praise icon (heart)
6_3   964 paths   -> punish icon (shouting face)
```

Because the regex only looked at paths, all three were silently absent from the
map. The result was three icons that never appeared no matter what the ROM did,
while everything else rendered correctly -- a failure mode that looks like
missing artwork rather than a missing map entry.

Fixed by extracting any element type with a segment id, collecting every `d=`
inside its span, and rasterising all of them as one segment. A guard now
compares the set of ids in the SVG against the set extracted and prints a
warning if anything was skipped, so this class of omission cannot recur
silently.

### Verified

Rendering the three in isolation produces the bat-and-ball, the heart and the
shouting face. Confirmed in play: with all 970 present, the icons behave
correctly during a full care cycle (medicine, cleaning, praise).
