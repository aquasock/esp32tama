# Tama-Go: status

## The CPU is now verified

`core/conformance.c` runs **Klaus Dormann's 6502 functional test** against
`gplb.c`. It exercises every documented opcode and addressing mode, and traps at
a specific address when one is wrong.

```
PASS: reached success trap at pc=3469 after 30,646,176 instructions
```

30.6 million instructions, no errors. `$3469` is the suite's own success trap,
labelled "test passed, no errors".

**This matters because the core had been derived from one that was validated
bit-exactly against a reference emulator — and it was still wrong.** Four real
bugs, each found at a specific address:

| Trap | Cause |
|---|---|
| `$063D` | Status bit 5 not always set. Not a flag on a real 6502, a constant |
| `$0674` | Status bit 4 not set by PHP. B is always pushed set |
| `$37D5` | BRK pushed PC+1; a real 6502 pushes PC+2, skipping the padding byte |
| `$283D` | ROR set Z from the old value and N from the old carry |
| `$283D` | ROR zp,X advanced PC by 2 — a reference bug reproduced for the V3 |

The last two are especially instructive. Both were *deliberately* reproduced
from BrickEmuPy because the V3's ROM was timed against that implementation.
They are correct for the SPLB32 and wrong for anything else. Deriving a core
from a bug-compatible one carries the bugs forward silently.

## The Tama-Go firmware now runs without crashing

Before the CPU fixes, the firmware crashed at instruction ~240,660: one more RTS
than JSR, then the PC walked through uninitialised RAM executing `BRK` bytes.

After them:

```
reset:                pc=CC00  sp=FF  p=34
after 400,000 steps:  pc=D27E  a=29  y=14  sp=FF  p=34
```

**Stack balanced at `sp=FF`**, and the program counter cycles through the
ROM's own copied loop in low RAM (`$0022`, `$0029`, `$009D`) with returns through
`$D27E` (the IRQ vector). That is a stable idle loop, not a crash.

28 IO registers are initialised, and the values match the disassembly.

## What is still missing: the display

Display RAM stays empty. The firmware initialises the hardware, then idles.

The remaining work is peripheral modelling:

1. **LCD controller** — 64 segments x 32 commons, 4 grey levels, display RAM at
   `$1000`, FP interrupt. Datasheet-described but with no register table.
2. **Timers** — TBL/TBH, TIMER0, TIMER1. Without these the ROM's clock never
   advances, which may be why it idles.
3. **Buttons** through the port registers.
4. **SPU** for sound.

## The real obstacle, stated plainly

The 28-page datasheet documents the display, memory map and electrical
characteristics, but **not the IO register map**. Registers are named inline
(`P_3001H_ClkCtrl`) with no address table and no bit definitions. The full
semantics are in GeneralPlus' programming manual, which is not public.

`gplb_io_trace` captures the firmware's own initialisation sequence, which is
ordered and readable:

```
$3001 <- 82    then $3000 <- 03/00 toggling
$3002 <- 80    $3070 <- 80, 04, 84
$3004 <- FF    $3030-$304A written as blocks
```

Reconstructing register semantics from that is possible but slow. It is
research, not porting, and its size is unknown.

## What changed in the assessment

Earlier I wrote that the CPU being standard made this nearly free. That was too
optimistic in one way and right in another:

- **Right:** the instruction set really is standard, and once the CPU was
  correct the firmware stopped crashing.
- **Too optimistic:** "the CPU is correct" was assumed rather than tested. It
  was not correct, and the conformance suite was the only thing that revealed
  it. Every peripheral fix built before that would have been built on sand.

The lesson generalises: a core derived from a bit-exact reference is bit-exact
*against that reference*, which is not the same as correct.

## Next

Model the timers and the LCD controller in that order — timers first, because if
the idle loop is waiting on a clock, that is cheaper to discover than a display
bug. Then a segment map for the 2048-dot panel, which needs a new input path for
`gen_segmap.py` since there is no `.brick` asset for this device.
