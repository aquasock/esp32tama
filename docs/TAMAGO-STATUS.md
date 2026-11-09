# Tama-Go: status

## Where it stands

The firmware boots, bank-switches correctly, initialises its hardware, and
reaches a **legitimate idle loop waiting on an interrupt**.

```
2M:  pc=769C  sp=F8  bank=3  display RAM=0  flag $01A7=00
20M: pc=769C  sp=F8  bank=3  display RAM=0  flag $01A7=00
```

Stable for 20 million instructions. No stack growth, no wandering PC.

The loop is:

```
769C: 2C A7 01    BIT $01A7     ; test a flag in RAM
769F: 10 FB       BPL $769C     ; spin until bit 7 is set
```

`$01A7` bit 7 is set by an interrupt handler at `$5271`. **No interrupt ever
fires, because the timers and LCD controller are not modelled.** That is the
entire remaining blocker.

## The CPU is verified

`core/conformance.c` runs Klaus Dormann's 6502 functional test.

```
PASS: reached success trap at pc=3469 after 30,646,176 instructions
```

Four real bugs found there, all inherited from a core that had been validated
bit-exactly against another emulator — which is not the same as being correct:

| Trap | Cause |
|---|---|
| `$063D` | Status bit 5 not always set (it is a constant on a real 6502) |
| `$0674` | PHP did not set bit 4 |
| `$37D5` | BRK pushed PC+1 instead of PC+2 |
| `$283D` | ROR zero flag from the old value; ROR zp,X advancing PC by 2 |

## The memory map was wrong, in three ways

Each of these cost real debugging time and each was found by tracing execution,
not by reading the datasheet more carefully.

### 1. The bank window read 16 KB too low

`read8` did `(addr - 0x4000) + rom_bank` when the image already places bank 0 at
file offset `0x4000`. Every read in `$4000-$BFFF` returned the fixed bank
instead of the selected one, so the firmware executed garbage and wrecked its
stack at instruction ~238,000.

### 2. `$3000` is the ROM bank register

Established from the firmware itself:

```
D294: A5 28     LDA $28       ; bank number
D296: 8D 00 30  STA $3000     ; bank register
D299: A5 3A     LDA $3A
D29B: 20 A9 D2  JSR $D2A9     ; -> JMP ($0026)
D2A9: 6C 26 00  JMP ($0026)   ; -> $B383 in the new bank
```

Without it, dispatch jumps into blank ROM. With it, execution continues.

### 3. `(Bank)L` and `(Bank)H` are two halves of one bank

The datasheet names `$4000-$7FFF` "(Bank)L" and `$8000-$BFFF` "(Bank)H", which
reads like two independent registers. The firmware treats them as two halves of
one 32 KB bank: bank 3 addressed at `$B383` resolves to offset `0x7383`.

## Verified by disassembly

The reset vector is `$CC00`, matching the datasheet's user-program start. It
decodes as textbook 6502 startup with **zero undefined opcodes**, and 90.7% of
the 6,315-instruction user region decodes cleanly.

IO values after init match what the disassembly says should be stored:

```
$3001=82  $3002=80  $3004=FF  $3010=88  $3011=10
$3012=1F  $3014=01  $3015=7E  $3016=07  $304A=03
```

## What remains

In order:

1. **Timers** (TBL/TBH, TIMER0, TIMER1). The idle loop waits on an interrupt;
   the simplest source to model is a timer. `$3010-$3016` look like timer
   setup (reload values and control) from the initialisation sequence.
2. **LCD controller.** Display RAM at `$1000`, 64x32 dots, 4 grey levels. The
   frame interrupt is the other candidate for `$01A7`.
3. **Segment map** for the 2048-dot panel — a new input path for
   `gen_segmap.py`, since there is no `.brick` asset for this device.
4. **Buttons and SPU.**

## The honest obstacle

The 28-page datasheet documents the display, the memory map and the electrical
characteristics, but **not the IO register map**. Registers appear only as names
inline (`P_3001H_ClkCtrl`) with no address table and no bit definitions. The
full semantics are in GeneralPlus' programming manual, which is not public.

Everything learned about IO so far came from reading the firmware:

- `$3000` — ROM bank register (confirmed by the dispatch sequence)
- `$3001` — clock control, `$82` at init, `$07` for standby per the datasheet
- `$3010-$3016` — written as a block with `$88 $10 $1F $01 $7E $07`; pattern
  looks like timer reload values and control
- `$3070`, `$3071`, `$3072-$3076` — display or interrupt control

Reconstructing the rest is empirical work of unknown size. That is the real
reason this is not simply "another 6502 port".

## Interrupts and the clock tick (done)

The firmware is driven by a periodic tick, and it took three findings to model
it correctly. All three are measured, not guessed.

**The vector table has sixteen entries, not twelve.** `$FFC0-$FFDF`, two bytes
each. The index is the source number, and the acknowledge confirms the order:
vector[3] acks `$10` (`$3073` bit 4), vector[7] acks `$01` (bit 0), vector[10]
acks `$20` (`$3074` bit 5), vector[13] acks `$04` (`$3074` bit 2). Four
independent handlers, one rule: `$3073`/`$3074` bit `7-(v&7)`.

**The tick arrives as TIMER1 underflow, and it raises both NMI and vector 10.**
The datasheet lists TIMER1 among the NMI sources. The NMI handler at `$D26B` is
the only code that sets `$01A7=$80`, and the boot path at `$769C` spins on
exactly that. Later the main loop at `$CE75` spins on `$2D`, which only the
vector-10 handler at `$D166` sets. One hardware event, two waits.

**The TIMER1 reload is written high byte first.** `$3014` is the high half,
`$3015` the low. Read the other way round the counter latches `$7E01` instead
of `$017E`, and the tick falls to 1.5 Hz. With the byte order fixed and a 256
cycle prescaler the reload of `$017E` gives 61.2 Hz, which matches a 64-common
LCD frame.

**`$3076` is write-one-to-set.** A plain-write reading deadlocks the firmware:
the boot path writes `$82` at `$7699`, then `$76BB` writes `$00`, and every NMI
handler writes `$00` again, so nothing is ever enabled. Measured with zeros
honoured: `irq=0` across 20M instructions and a frozen screen. With the latch:
the main loop body executes, `$2D` toggles, and the ROM changes bank 293 times
in 4M instructions. So the firmware's own behaviour rules out plain writes.

## Assessment

The CPU is verified bit-exact (Klaus Dormann's suite, `pc=$3469`, 30,646,176
instructions — unchanged by the interrupt work). The memory map is correct for
what the firmware uses, the interrupt controller is modelled, and the tick runs
at the right rate.

The firmware now reaches its main loop at `$CE3C`/`$CE66` and cycles it, rather
than spinning in the boot wait or crashing.

**No picture yet.** Display RAM is written exactly once and then frozen. The
write trace attributes every byte:

- **512 bytes** cleared at `$CC9A` — a memset loop filling `$1000-$11FF` with
  `$00` before the timers are programmed at `$CCA3`.
- **372 bytes** blitted at `$F081`, inside `$F050` — 12 bytes per row across 31
  rows, driven by two pointer tables at `$F0FE` and `$F13C`.
- **3 bytes** from `$F0E7`/`$F0ED`/`$F0F3`.

After that, nothing. `$F050` is gated on RAM `$2F`: it blits only when `$2F` is
non-zero with bit 7 clear, and otherwise falls to `$F0F9`, which just clears
`$2F` and returns. The ROM therefore believes the screen is current and calls
it once per frame purely to check.

Decoding the two tables fixes the geometry. Both are big-endian 16-bit
pointers, and the source is simply contiguous:

- source `$0400 + 12*i`, 31 rows — a 372-byte framebuffer in plain RAM
- destination `$100C` upward for rows 0-15, then `$10B4` downward for rows
  16-30 — the same 372 bytes of LCD RAM, written outward from the middle

**LCD RAM is 512 bytes, not 2048.** The datasheet says so twice ("LCD RAM 512
bytes", "512 bytes dedicated LCD RAM") and the arithmetic confirms the dot
format: 512 bytes x 8 bits / 2048 dots = **2 bits per dot**, which is exactly
the "4 gray levels" the feature list promises. Display RAM is 64 columns x 32
commons at 2 bits per dot. The earlier assumption of one byte per dot was wrong
and is what made the memory map look four times too large.

Rendering the source buffer at its native 12-byte pitch (96 bits per row) gives
something legible: a rectangular frame with corner ticks, a bounded region with
evenly spaced dashes, and a circle in the lower middle. That is an **LCD test
pattern**, not a scene — so the firmware is drawing to prove the panel works and
then stopping.

Rendered at 48 dots wide (12 bytes x 4 dots at 2bpp) the buffer resolves into
distinct UI shapes rather than noise, so the row pitch is right even though the
48-dot width does not match the datasheet's 64 segments. Treat 12 bytes as
measured and the dot width as still open.

## What the firmware actually reads

Only five IO registers are ever read, which narrows the peripherals a long way:

```
$3000   368       ROM bank, saved and restored around every handler
$3012   734       read during the timer setup paths
$3016   71145     the hot one - a port with latched outputs and sensed inputs
$3032   1         IR reply byte
$3033   1         IR reply byte
```

`$3016` being read 71,145 times and written back as a read-modify-write settles
what it is: an **I/O port**, not a status register. Its handling at `$C651`
reads the pins, writes the value to `$3077`, tests bit 6, and writes the value
back with bit 6 optionally set — a debounced key scan with an output latch. So
the 71k reads are button polling, and returning a constant `$07` makes the ROM
see a fixed, never-changing key state.

The RTC, by contrast, is never read at all. That rules out "waiting for the
clock to be set" as the reason the scene does not appear: there is no time
register traffic to wait on.
