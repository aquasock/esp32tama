# Attack surface

What this device can actually communicate through, which of those the running
firmware services, and what has actually worked on this product line before.

## Inventory

| Interface | Present | Serviced by firmware | Needs no soldering |
|---|---|---|---|
| **Infrared (IR)** | yes | **yes** | **yes** |
| Test pads `CLK`/`DATA`/`RESET2`/`P0.12` | yes | no — measured dead | no |
| Speaker (`SPL`) | yes | yes, output only | no |
| USB | no | — | — |
| BLE / WiFi | no | — | — |
| NFC | no | — | — |

The 2024 Connection is **IR-only**. That distinguishes it from the Friends (NFC)
and the Uni (WiFi). Exactly one communication interface, and it is one the
firmware actively listens to.

## Correction — IR has never produced a dump on this product line

An earlier version of this document implied IR was the historical way in. That
was wrong. Checking Natalie Silvanovich's write-ups directly:

| Device | ROM dump method | IR involved? |
|---|---|---|
| TamaTown Tama-Go (2010) | code execution via the **figure/EEPROM contacts** (smartcard-like pads), then ROM dump | **no** |
| Tamagotchi Friends (2013) | **voltage glitching** | **no** |
| Tamagotchi Friends EEPROM | wires soldered to the EEPROM, Arduino `Wire` (I2C) | **no** |
| Connection V3 (2006) | decap + die photography (azya) | **no** |

Her Tama-Go teardown is explicit: the board had "the same IR transmitter and
receiver" and "no visible debug pads" — and the vector she pursued was the
figure contacts, not the IR window.

She also found the Friends' data parsers considerably harder to break than the
Tama-Go's. Bandai hardened them after 2010, and the 2024 parser should be
assumed hardened too.

## What has actually worked, ranked

### 1. Voltage glitching — the only proven route on a Generalplus-lineage part

The Friends' ROM was dumped by glitching, with no firmware bug involved. That
matters here:

- It worked on the same chip lineage as the 2024 unit.
- It attacks the silicon, not the firmware, so a hardened parser is irrelevant.
- It needs no software interface, so a dead test pad is irrelevant.

Cost: a crowbar glitcher (a Pico with a MOSFET is enough), an interruptible
supply, and a lot of patience. Timing-sensitive, and it can brick the target —
so a donor unit is worth buying first.

This is now the highest-probability route and it was not on the earlier list.

### 2. IR parser fuzzing — cheap, but dim prospects

IR is the only data input the firmware trusts. Historical precedent for
attacking that kind of input is poor on this product line, but it costs almost
nothing to try in the emulator first.

a. Capture a real exchange between two devices to confirm framing on this
   revision.
b. Reimplement the checksum; verify by replaying a captured frame and seeing the
   target accept it.
c. Drive the 2006 V3 ROM's IR peripheral in BrickEmuPy with mutated frames and
   watch for parser faults.

### 3. Decap and die photography — known to work, slow and destructive

The 2006 V3 method. Weeks of bit extraction at 512 KB. Needs a donor unit.

### Dead end, do not revisit

The test pads. Six pins, four protocols, four power cycles, zero traffic.
Documented in `docs/findings.md`.

## Physical layer of the IR link

38 kHz modulated carrier, handled in hardware by the receiver module and by a
transistor driving the emitter (`Q1` on this PCB).

NEC-like framing, in microseconds of carrier on/off:

```
header pulse      9500
header gap        6000
bit 0             600 on,  600 off
bit 1             600 on, 1200 off
trailer           1200
```

Decode is a threshold test on the gap: under ~900 us is a 0, under ~2000 us is
a 1.

## Link layer

- 20-byte messages
- 4 messages per exchange
- a checksum field is present

Payload semantics are documented by the tamagometer project (`Protocol.md`):
interaction type per message index, device ID, character, 5-letter name, gender,
care level, gift item, game selection, wager.

**The checksum is the gatekeeper.** Any fuzzing must produce frames the target
will accept, so that routine has to be reproduced first.

## Emulator experiment — 2026-11-08

Tested whether BrickEmuPy's 2006 V3 IR path can be driven from the host, which
would allow fuzzing with no hardware.

### What works

`peripherals/CON_V3_IR.py` exposes `serial_rx_handler(data)`, which queues bytes
for the emulated receiver. Feeding it 20 bytes produces real pin activity: `PB`
bit 2 (the IR receive line) toggles and the state machine shifts bits out.

**Gotcha:** the CPU must be stepped together with the interconnect:

```python
c = cpu.clock()
ic.emit_clock(c)      # without this the IR peripheral never advances
```

The UI's `emulator_process.py` does this; a bare `cpu.clock()` loop silently
skips every peripheral. An earlier attempt failed for exactly this reason and
looked like the ROM ignoring IR.

### What does not work yet

Injecting a frame does not visibly change the display, and 12 random 20-byte
frames produced no illegal-instruction traps and no emulated IR response.

Likely reasons, both addressable:

1. The ROM boots to the egg and only samples IR meaningfully from the connection
   screen. Reaching that needs the button inputs driven through the menu.
2. A conversation is 4 messages, not 1. A single burst may be discarded as
   incomplete.

The parser is reachable in principle but not yet exercised.
