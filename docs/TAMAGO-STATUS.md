# Tama-Go: what was established, and where it stands

## The finding that matters: it is a standard 6502

The Tama-Go's GeneralPlus GPLB52320A is **not** an unknown architecture. It
executes standard 6502 opcodes.

Established by disassembling the firmware from its reset vector using the
opcode table Natalie Silvanovich published in
`natashenka/Tamagotchi-Hack/disassembler/`, which lists both encodings side by
side (`ADC #dd` is `69H` on 6502 and `56H` on SunPlus).

The reset vector reads `$CC00`, exactly as the datasheet says ("User program
starts from $CC00"). Decoding from there:

```
CC00: 78          SEI
CC01: A2 FF       LDX #$FF
CC03: 9A          TXS
CC04: A9 FF       LDA #$FF
CC06: 8D 04 30    STA $3004
CC09: A9 82       LDA #$82
CC0B: 8D 01 30    STA $3001
CC0E: A9 80       LDA #$80
CC10: 8D 02 30    STA $3002
```

That is textbook 6502 startup: disable interrupts, set the stack, then program
I/O registers at `$3000-$3FFF`.

**Zero undefined opcodes** in the first 40 instructions, and a **90.7% decode
rate** across the whole user-program region (`$CC00-$FFBF`, 6,315 instructions),
with the residue being data interleaved with code.

### A false start worth recording

The table has two opcode columns. Using the wrong one (the GeneralPlus assembler
numbering rather than the machine encoding) produced 22 undefined opcodes out of
40 and obvious nonsense. The right column produces clean code. The disassembler
script defaults to the wrong one and says to comment out a line to get the
standard set — so the mistake is pre-armed.

## The core

`core/gplb.c` — 603 lines, derived from the SPLB32 core because the instruction
sets are the same.

| | SPLB32 (V3) | GPLB (Tama-Go) |
|---|---|---|
| ISA | 6502 with quirks | standard 6502 |
| Clock | 4 MHz | 6 MHz typical |
| ROM | 512 KB | 320 KB or 640 KB |
| RAM | 1,216 B | 1,536 B |
| Display RAM | 512 B | 2,048 B |
| I/O | SFR bank at `$0000` | registers at `$3000` |
| Cycle table | 13 entries were V3 bugs | datasheet values |

**138 of 151 cycle counts were already the same.** The 13 that differed were
exactly the reference bugs reproduced for the V3 (`ROR zp,X` PC advance, the ROR
zero flag, `JMP (ind)` timing and so on). Here they are the standard values.

## It boots

```
reset:                pc=CC00  sp=FF  p=04
after 200,000 steps:  pc=D462  a=07  x=D8  y=13  sp=FD  p=85
```

Ten I/O registers written, and **every one matches the disassembly byte for
byte**:

```
IO 3001 = 82     IO 3004 = FF     IO 3011 = 10     IO 3015 = 7E
IO 3002 = 80     IO 3010 = 88     IO 3012 = 1F     IO 3016 = 07
IO 3014 = 01     IO 304A = 03
```

That is the strongest evidence available that the CPU is executing the real
firmware correctly: the values the emulator's registers hold are the values the
disassembly says should be there.

## Where it stops, and why

The display RAM stays empty. Over 5,000,000 instructions the program counter
cycles between low RAM (`$0021`, `$0026` — code the ROM copies there) and
`$D27E`, which is a bare `RTI`.

The cause is not the CPU. **The peripherals are stubs:**

- the LCD controller is not modelled, so the ROM's polling of its status
  registers never reports the display ready
- the timers are not modelled
- the buttons are not connected
- `io_write_side_effects()` accepts writes and does nothing

So the firmware initialises, then waits for hardware that never answers.

## What remains

1. **LCD controller.** The datasheet documents it: 64 segments x 32 commons,
   4 grey levels, display RAM at `$1000`, and the frame (`FP`) interrupt. This
   is the piece that yields a picture.
2. **Segment map** from the 2048-dot panel — a new input path for
   `gen_segmap.py`, since there is no `.brick` asset for this device.
3. **Timers** (TBL/TBH, TIMER0, TIMER1) so the ROM's clock advances.
4. **Buttons** through the port registers.
5. **SPU** for sound.

## Honest assessment

The hard part — "what CPU is this and what does it execute" — is answered, and
the answer is favourable: it is a 6502, and the code is already 90% decoded.
The CPU core works and is verified against the disassembly.

The remaining work is peripheral modelling, which is ordinary engineering with a
manufacturer datasheet in hand. That is a different proposition from the V3,
where the ISA had to be inferred from a reference emulator.

**But it is not done, and it will not show a picture until the LCD controller is
modelled.** Nothing here should be presented as a working Tama-Go emulator.
