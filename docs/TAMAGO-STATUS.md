# Tama-Go: status

Supersedes the earlier version of this file. Written after instrumenting the
IO writes and tracing the failure.

## Established: it is a standard 6502

The GPLB52320A executes standard 6502 opcodes. Established by disassembling the
firmware from its reset vector using the opcode table in
`natashenka/Tamagotchi-Hack/disassembler/`.

The reset vector reads `$CC00`, matching the datasheet's "user program starts
from $CC00". Decoding from there gives textbook startup:

```
CC00: 78          SEI
CC01: A2 FF       LDX #$FF
CC03: 9A          TXS
CC04: A9 FF       LDA #$FF
CC06: 8D 04 30    STA $3004
```

**0 undefined opcodes** in the first 40 instructions. **90.7% decode rate**
across the 6,315-instruction user region.

## The core boots and executes real firmware

`core/gplb.c`, 603 lines, derived from the SPLB32 core. 138 of 151 cycle counts
were already identical; the 13 that differed were the reference bugs reproduced
for the V3, and here they carry the datasheet values.

After 200,000 instructions the IO registers hold exactly what the disassembly
says should be there:

```
$3001=82  $3002=80  $3004=FF  $3010=88  $3011=10
$3012=1F  $3014=01  $3015=7E  $3016=07  $304A=03
```

## Where it fails, and the evidence

Execution enters a crash at instruction ~240,660. The evidence:

```
stack rising past calls at instr 240660 pc=D27E sp=FE jsr=2146 rts=2147
```

One more RTS than JSR. Then the PC walks through uninitialised RAM:

```
D27E 05FB D27E 05FC D27E 05FD D27E 05FE D27E 05FF D27E 0600 0602 0604 ...
```

`D27E` is a bare `RTI` (the IRQ vector target). The bytes at `$05FB-$05FF` are
`00` — `BRK` — so each one pushes a return address and vectors back to the RTI,
walking forward one byte at a time until it reaches real code at `$0600`.

That is a crash, not a wait loop. Something before instruction 240,660 took a
wrong branch that unbalanced the stack.

## The likely cause, stated as inference not fact

The ROM branches on status it reads from the IO block. With
`io_write_side_effects()` doing nothing and reads returning only the latched
write value, any "is the hardware ready" poll returns whatever was last written
rather than a plausible status. A wrong branch from there is exactly the kind of
thing that unbalances a stack.

**I have not proven this.** The alternative is that my 6502 core has a bug in a
rarely-exercised path, and the ROM is fine.

## The two things to do next, in order

### 1. Validate the CPU independently

The core is derived from one that was validated bit-exactly against a reference,
but the *changes* — the GPLB memory map, the cycle table, the page-cross and
branch timing — are unvalidated. A standard 6502 is lucky here: **Klaus
Dormann's `6502_functional_test`** exercises every documented opcode and
addressing mode and reports a specific failure address. Running it against
`gplb.c` would settle whether the CPU is correct without needing the ROM.

That is the right next step and it is cheap. Do it before touching peripherals,
or a peripheral fix will be built on an unverified foundation.

### 2. Then model the LCD controller and timers

The datasheet documents the display (64 segments x 32 commons, 4 grey levels,
display RAM at `$1000`, FP interrupt) but **not the IO register map** — the
28-page datasheet names registers inline (`P_3001H_ClkCtrl`) without a table.
The full register semantics live in GeneralPlus' programming manual, which is
not public.

The workaround is empirical: the firmware's own initialisation sequence is
captured by `gplb_io_trace`, and the register writes are ordered and readable:

```
$3001 <- 82      $3000 <- 03/00 toggling (enable?)
$3002 <- 80      $3070 <- 80, 04, 84
$3004 <- FF      $3032/$3033, $3040-$304A written as a block
```

Reconstructing semantics from that is possible but slow, and it is the honest
reason this is a research problem again rather than a mechanical port.

## What I would tell someone asking "can I play Tama-Go in this yet"

No. The CPU executes the firmware's real startup correctly, which is a real
result, but nothing renders and the firmware crashes 240,000 instructions in
because the hardware around the CPU is not modelled.

The V3 emulator is a finished thing that works. This is a promising start with
two substantial pieces of unknown size in front of it.
