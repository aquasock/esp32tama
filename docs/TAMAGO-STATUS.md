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

## Assessment

The CPU is done and verified. The memory map is now correct for what the
firmware uses. The firmware runs to a stable, meaningful wait state rather than
crashing.

**No picture yet**, and the display will not appear until the timers or the LCD
frame interrupt are modelled. But the emulator is now blocked on a specific,
named thing rather than on unknown breakage.
