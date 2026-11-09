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

The firmware reaches its main loop at `$CE3C`/`$CE66` and cycles it rather than
spinning in the boot wait or crashing, and with the figure connector modelled it
enters the gameplay path — see *The gate is the figure connector* below.

**Still no picture.** In the gameplay path the screen is cleared and re-drawn
2.1M times but never painted, because an undocumented handshake at `$3008`
never completes.

The boot-time display writes are still worth recording, because they fix the
geometry. The trace attributes every byte:

- **512 bytes** cleared at `$CC9A` — a memset loop filling `$1000-$11FF` with
  `$00` before the timers are programmed at `$CCA3`.
- **372 bytes** blitted at `$F081`, inside `$F050` — 12 bytes per row across 31
  rows, driven by two pointer tables at `$F0FE` and `$F13C`.
- **3 bytes** from `$F0E7`/`$F0ED`/`$F0F3`.

After that, nothing. `$F050` is gated on RAM `$2F`: it blits only when `$2F` is
non-zero with bit 7 clear, and otherwise falls to `$F0F9`, which just clears
`$2F` and returns. The ROM therefore believes the screen is current and calls
it once per frame purely to check.

Decoding the two tables fixes the geometry. Both are 16-bit pointers stored
**high byte first** — the loader at `$F066` takes byte `2i` as the high half and
byte `2i+1` as the low half, which is the reverse of what a naive little-endian
read gives and is easy to get wrong.

- source `$0400 + 12*i` for 31 rows — a contiguous 372-byte framebuffer in
  plain RAM
- destination `$10C0`, `$10CC`, ... `$1174` for rows 0-15 (ascending), then
  `$10B4`, `$10A8`, ... `$100C` for rows 16-30 (descending) — one contiguous
  372-byte block, `$100C-$117F`, filled from both ends inward

The two halves do not overlap: 16 ascending rows plus 15 descending rows cover
the block exactly once.

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

**The 12-byte row does not span the panel.** At 2 bits per dot, 12 bytes is 48
dots, against 64 segments in the datasheet. So this blit is a 48-dot-wide,
31-row window — three quarters of the width and all but one common — rather
than a full-frame copy, and the block starts at `$100C` rather than `$1000`.
Treat 12 bytes and 31 rows as measured, and the remaining question as *which*
sub-rectangle of the panel `$100C` maps to, not as a contradiction.

## What the firmware actually reads

The read census differs sharply between the two paths, which is itself
evidence that they are genuinely different code.

Without a figure (the stall path):

```
$3000   369       ROM bank, saved and restored around every handler
$3012   736       figure sensor bits 0-2, and two debounced status bits
$3016   71243     TIMER1 control - bit 0 polled by the loop at $AEB9
$3032   1         unused in this path
$3033   1
```

With a figure (the gameplay path, 30M instructions):

```
$3000   1039      ROM bank
$3008   4202      handshake status, bits 0-1 (see below)
$3012   25731     figure sensor + status
$3032   1
$3033   1
$3060   4203      polled by the loop at $F629 until it goes non-zero
```

Note `$3016` disappears entirely — TIMER1's control register is a boot-path
concern only, which confirms it was never a button port.

## `$3016` is TIMER1's control register, not a button port

An earlier note here called `$3016` a key port on the strength of its read
count. That was wrong, and the disassembly settles it:

- `$3014`/`$3015` are written as a pair immediately before `$3016` every time,
  e.g. `$3015 <- $7E`, `$3014 <- $01`. Together with the 256-cycle prescaler
  that is the `$017E` reload that gives the 61 Hz tick.
- `$C318` and `$C326` clear all three registers together, which is what a
  "shut this timer block down" helper looks like.
- `$3016` is written from the RAM shadow at `$21` (`LDA $21 ... STA $21 /
  STA $3016`), and the values cycle `$04`/`$05`/`$06`/`$07` — bit 0 plus a
  two-bit field in bits 1-2.

The 71,145 reads come from one loop at `$AEB9`:

```
AEB9: LDX #$00
AEBB: DEX
AEBC: BEQ +$2E        ; give up after 256 tries
AEBE: LDA $3016
AEC1: AND #$01
AEC3: BNE $AEBB       ; spin while the run bit is set
...
AEC5: LDA #$7F
AEC7: STA $3015       ; reload low
AECA: LDA #$01
AECC: STA $3014       ; reload high
AECF: LDA $21
AED1: AND #$FD
AED3: ORA #$01
AED5: STA $21
AED7: STA $3016       ; re-arm
```

So bit 0 is a run/busy flag that the ROM waits to clear, with a bounded
timeout. One timer period is about 98,000 CPU cycles; 256 iterations of that
spin is about 2,500 cycles. **The timeout therefore always expires first** —
the ROM times out on essentially every pass, which is exactly what the 71,145
figure looks like (about 278 passes over 11M instructions). That is normal
behaviour for this loop, not a symptom of a missing peripheral.

The consequence for the model is that the tick must keep running across the
`$04`-`$07` control writes. Gating the timer on bit 0 stops it mid-run and
hangs the boot path at `$769C`; that was measured, not assumed. What is
implemented now: TIMER1 free-runs while its reload is non-zero, zeroes it only
when the reload registers are cleared, and drops bit 0 of `$3016` at each
expiry so the poll loop sees the flag it is waiting for.

Measured with that model: 98,398 cycles per expiry, **61.0 Hz**, against a
64-common LCD frame. The CPU is unaffected — Klaus Dormann's suite still passes
at `pc=$3469` after 30,646,176 instructions.

## Verification

`core/tamago_check.c` asserts the display model against the firmware's own
blit, so a regression shows up as a failure instead of a subtly wrong picture:

```
gcc -O2 -o tamago_check core/tamago_check.c core/gplb.c
./tamago_check /path/to/tamago.rom
```

It checks that the 31-row blit reproduces the `$0400` framebuffer byte for
byte, that the destination addresses stay inside LCD RAM and never double up,
and that the TIMER1 tick lands in the frame band. All five pass:

```
PASS blit: LCD RAM matches the $0400 framebuffer
PASS geometry: all blit destinations are inside LCD RAM
PASS geometry: no destination byte is written twice
PASS geometry: 31 rows x 12 bytes covered
     TIMER1 tick: 61.0 Hz (364 expiries)
PASS timer: tick is in the 50-75 Hz frame band
OK
```

The geometry the checks confirm: 31 rows of 12 bytes, at 2 bits per dot, in a
contiguous 372-byte block. At 48 dots wide the block renders as a bordered
panel with vertical rules, a bounded band of evenly spaced dashes, and a circle
— a diagnostic layout, not a scene.

## The gate is the figure connector

The screen used to stop at the boot diagnostic pattern because the emulator
looked to the ROM like a Tama-Go in an unplayable state. That is now diagnosed
and modelled.

**`$3012` bits 0-2 are the three figure-connector contacts, and they read as
inputs.** The ROM writes `$1F` to `$3012`, which sets pull-ups on those lines,
and then reads them back. `$E209` inverts and debounces the reading:

```
E209: LDA $3012 / EOR #$07 / AND #$07 / STA $49
      JSR $D460                    ; short delay
      LDA $3012 / EOR #$07 / AND #$07
      CMP $49 / BNE $E209          ; retry until two reads agree
      LDA $49 / RTS                ; returns ~$3012 & 7
```

So it returns `~$3012 & 7`: 7 when all three contacts are low, 0 when all three
are high. A figure clipped on pulls the contacts low, so a real unit with a
figure reads **000**, and one without reads **111**.

**The boot branches on it at `$CCE7`, and only 000 proceeds.**

```
CCE7: JSR $E209
CCEA: CMP #$07 / BNE $CCF5        ; not all-low -> the "no figure" path
CCEE: LDA #$41 / STA $22 / JMP $CD94
```

`$22 = $41` selects the gameplay path. `$CD94` is the real game init: `SEI`,
`JSR $D70B`, then the segment-driver block `$3064 = $80`, `$3060 = $00`,
`$3062 = $00`, `$3065 = $02`, `$3006 = $01`, and the game state at `$9D`,
`$97`, `$9C`, `$95`, `$B1`, `$A3`, `$A5`.

**Measured effect of modelling the contacts.** Before, `$0310` was never
written, `$22` stayed 0, and display RAM never changed again after the boot
blit (888 writes total). With the contacts low, the ROM enters the gameplay
path, executes bank 1 heavily (86,631 instructions in the window over 30M,
against 36,867 before), and display activity rises to **2,152,312** writes.

`gplb_set_figure(c, contacts)` exposes this: 0 means a figure is attached
(contacts low), non-zero means they read high. The default is 0.

## What still blocks a picture

The screen is cleared and redrawn but never painted, and the reason is the next
handshake down.

`$CF8E` polls `$3008` and selects on bits 0-1:

```
CF8E: LDA $3008 / STA $2E
CF93: AND #$02 / BEQ $CF9A
CF97: JMP $CFCB                    ; bit 1 set
CF9A: LDA $2E / AND #$01 / BNE $CFA3 ; bit 0 set
CFA0: JMP $CEE2                    ; both clear
```

`$CEE2` is a re-initialisation: `$3004 = $55`, `$3060 = 0`, `$3062 = 0`,
`$3006 = 0`, `$3011 = $10`, `$3010 = $88`, `$3012 = $1F`. With `$3008` reading
zero the ROM re-runs boot on every pass — that is where the 2.1M display writes
come from, not from a renderer.

Forcing `$3008` to 0, 1, 2, 3, 4, 8 and `$FF` changes which path is taken
(bit 1 lands at `$E812`, bit 0 at `$CE66`) but **none of them produces a drawn
frame**: the peak non-zero LCD content stays at 376 bytes, the boot pattern, in
every case. So `$3008` is necessary but not sufficient, and there is at least
one more condition.

The register block involved — `$3006`, `$3008`, `$3060`, `$3062`, `$3064`,
`$3065` — has no public documentation. `$3060` is itself polled 4,203 times in
the gameplay path by a loop at `$F629` that re-reads until it goes non-zero. The
most likely reading is that this is the **figure's communication channel**: a
Tama-Go figure has a small chip on it, and the console handshakes with it before
the pet appears. Without the figure's protocol implemented, the handshake never
completes.

That is the honest blocker: not a CPU, timer, interrupt, blit or geometry
problem — all five are verified — but an undocumented handshake with a
peripheral that is not in the dump.

## Tooling

`tools/disasm.py` disassembles the image with the fixed block and 32 KB bank
windows handled correctly:

```
./tools/disasm.py rom.bin 0xCC00 40           # fixed block
./tools/disasm.py rom.bin --bank 1 0x5900 20  # bank window
./tools/disasm.py rom.bin --vectors           # interrupt and hardware vectors
```

It reads `tools/gplb_ops.json`, the 151 standard-6502 encodings, and prints
branch targets already resolved to absolute addresses.

## Where the buttons are

There is no button port in the read census. The firmware reads only `$3000`,
`$3012`, `$3016`, `$3032` and `$3033`, and none of those is a key matrix. Keys
must therefore arrive as an interrupt source rather than by polling, which is
consistent with the interrupt controller being the front door for most of this
chip's peripherals.

The RTC, by contrast, is never read at all. That rules out "waiting for the
clock to be set" as the reason the scene does not appear: there is no time
register traffic to wait on.
