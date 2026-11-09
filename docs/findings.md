# Findings

Investigation of the **Tamagotchi Connection (2024, 20th anniversary)** board
marked `43400OTP-V7`, chip-on-board under epoxy.

Session date: 2026-11-08.

## Question

Can the firmware be read, or at least reached, through the exposed test pads?

## Answer, in one line

**No — not by any software-reachable route we could find.** Every protocol-level
probe was tested with a verified-working instrument and came back empty. The
pads are most likely factory test points that the running firmware never
services.

## What the pads are

| Pad | Status |
|---|---|
| `VDD` (x2, one unlabelled) | bonded, ~3.0 V rail, 10 ohm between the two points |
| `GND` (x2) | bonded, 0 ohm |
| `CLK` | bonded, open-drain, open to VDD, idle high |
| `DATA` | bonded, open-drain, open to VDD, idle high |
| `RESET2` | bonded, active low, works — holding it low reliably resets the chip |
| `P0.12` | bonded, idle high, function unknown |
| `SPL` | speaker cluster (beside `R9`, `Q2`), not SPI; read as "speaker" |
| `VPP` | 0 V under power, open to BN0 both ways — unbonded or a dormant rail; not resolvable without a 7th wire |
| `B` corner mark | open in both directions, a datum |

## What was tried, and what it returned

Each result below comes from an instrument that was first proven able to detect
a signal on that line.

| Hypothesis | Result |
|---|---|
| I2C slave | No ACK at 100 Hz, 1 kHz, 10 kHz, 100 kHz, 400 kHz |
| SWD target | ACK `111` consistently — an undriven line, not `001` |
| Mode-gated boot via `P0.12` | Low, high, floating across resets: no change |
| Slave waiting to be clocked | 1024 clock edges after reset: all ones, no response |
| Boot traffic, polled | No edges, but only ~2-3 kHz bandwidth |
| Boot traffic, PIO at 150 MHz | **Zero CLK edges** across several power cycles |
| DATA signalling without CLK | Zero edges over 8 s |
| Live device, LCD on, egg animating, rail verified 2967-3067 mV | **Zero edges on CLK and DATA** |
| Autonomous capture, **three power cycles**, rail decay/recover in log | **Zero edges on CLK and DATA**, no raw file written |

## The decisive runs

**A. Device live, LCD attached, egg visibly animating.** Both PIO trigger paths
active at 150 MHz for 15 s, ADC input enable cleared so the rail reading was
trustworthy.

```
CLK samples  : 0
DATA samples : 0
P0.12 chg    : 0
rail_mv      : 2967 - 3067
```

**B. Three power cycles in one 54 s autonomous capture**, running from `main.py`
on the Pico so it was independent of the host. The rail column shows each cycle
as a decay and recovery (3052->1028->2983, 3050->921->2988, 2957->1222->2991),
which proves the capture spanned the boot events.

```
clk_edges  : 0
data_edges : 0
no cap2.raw file was created - nothing to record
```

Run A: the rail stayed in a 100 mV band across the whole window, so the device
was powered and working.

**Consistent across two techniques and four power cycles.** That is the basis
for the conclusion, not any single run.

## Two measurement faults that had to be found first

Both were caught and corrected; they would each have produced a confident wrong
answer.

1. **RP2350-E9 defeated the pull-down test** (see below).
2. **The ADC input buffer corrupted the rail reading** — 1195 mV on a healthy
   3000 mV rail. Toggling the pad cleared it. Proof the wire was fine all along:
   with the internal pull-down on, GP26 still read 1, which a 36-113 kOhm
   pull-down cannot do against a real rail.

## The one result that was wrong, and why

An early pull-down test appeared to show all four pins "driven HIGH by the
target". That was invalidated by erratum **RP2350-E9** on the Pico side: a pad
in the undefined logic region sources ~120 uA and latches at ~2.2 V, which the
internal pull-down cannot overcome. Demonstrated on unconnected pins
(driven high then pull-down reads 1; never driven reads 0).

The corrected and better-supported statement: **open-drain is established.**
Driving each line low as an *output* wins against the leakage, and it read 0, so
the target is not driving these lines. They idle high through pull-ups.

## What would change the picture

1. **`VPP` is unresolved and deliberately left so.** It reads 0 V powered and
   open to BN0 in both directions, which is consistent with either an unbonded
   test point or an OTP rail that only rises under a fixture. A voltmeter cannot
   separate those, and the test that would (pull-up on a 7th wire) was judged
   not worth it: even a confirmed bond would not tell us the programming
   voltage, sequence or protocol, so it would not bring a dump closer.
2. **Decap.** The method that worked for the 2006 V3 (Sunplus SPLB32): expose
   the die, photograph the ROM array, extract bits. Destructive — buy a donor
   unit first.
3. **A logic analyser on a real boot**, as an independent check on the PIO
   result. Low priority: the PIO capture was self-validated and the bandwidth
   argument is sound.

## Honest limits

- These pads may still be a programming port requiring a precondition we did
  not guess — a specific mode sequence, a voltage on a pad we did not identify,
  or a fixture handshake. "We found no route" is not "no route exists".
- `VPP` was measured 0 V powered, once. It was not repeated and is not wired to
  the harness, so its status is genuinely unknown rather than merely uncertain.
- Everything was tested on a 3.0 V bench supply, not a CR2032. A rail-dependent
  boot behaviour is conceivable but unlikely.

## Decision point

The software route is closed on the evidence. Two ways forward, and no third:

- **Stop.** The device works and is physically preserved. The negative result is
  documented with a method that can be reproduced.
- **Decap.** The only route with a demonstrated success on this product line
  (SPLB32, 2006 V3). Weeks of work, destroys the chip, needs a donor unit.

`VPP` does not change that choice, which is why it was dropped rather than
chased.

## Reusable artifacts

Everything built here is independent of the Tamagotchi and reusable on other
boards:

- `tools/find_port.py` — locate the Pico by USB serial, survives replugging
- `tools/mp.py` — MicroPython raw-REPL driver, no mpremote needed
- `firmware/piocap.py` — self-validated PIO edge capture at system clock
- `firmware/main_autocap.py` — autonomous capture that survives host disconnects
- `tools/powercap.py` — polling capture across a power cycle
- `docs/electrical.md` — every measurement with conditions

Two traps documented there that will bite anyone using a Pico 2 as a probe:

1. **RP2350 pads power up pulling down.** An active-low target reset wire will
   hold the target dead, and the supply will read 0 A.
2. **The ADC reads ~1.2 V on a healthy 3.0 V rail** unless the GPIO input buffer
   is cleared for that pin first.
