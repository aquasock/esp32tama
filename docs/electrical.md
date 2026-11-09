# Electrical survey — board `43400OTP-V7`

Device: Tamagotchi Connection, 2024 20th-anniversary release.
Package: chip-on-board, epoxy blob. LCD flex **removed** during measurement.
Battery removed, board unpowered. Meter: diode mode unless stated.

Meter test current unknown; treat absolute values as relative. Grouping and
direction matter more than the exact number.

## Test 1 — forward diode, red probe on pad, black on negative battery terminal

| Pad | Reading |
|---|---|
| `GND` | 0.00 V |
| `VDD` | 2.00 V |
| `VPP` | open |
| `CLK` | 1.26 V |
| `DATA` | 1.18 V |
| `RESET2` | 2.70 V |
| `P0.12` | 1.94 V |
| unlabelled round pad | 2.01 V |
| `SPL` | ~2.65 V |
| `B` (corner mark) | open |

## Test A — reverse diode, black probe on pad, red on negative battery terminal

| Pad | Reading |
|---|---|
| `CLK` | 0.45 V |
| `DATA` | 0.45 V |
| `P0.12` | 0.47 V |
| `RESET2` | 0.45 V |
| unlabelled round pad | 0.35 V |
| `VDD` | 0.34 V |

## Test B — pull-up search, ohms pad to `VDD`

All signal pads: **open**. No pull-ups present on the board.

Exception: the unlabelled round pad reads **10 ohm** to `VDD`, the same as the
positive battery terminal reads to `VDD`.

## Test C — net aliases, ohms pad to pad

`CLK`/`DATA`, `P0.12`/unlabelled, `SPL`/`RESET2`: **all open.**

## Test D — `VPP` under power

**Not performed.** Outstanding.

## Interpretation

**Every signal pin is bonded.** All four conduct in both directions through
stacked junctions. A pad with no path in either direction (`VPP`, `B`) is
unbonded — those are fixture datums or bare test points.

**No pull-ups, no aliases.** The four signals are independent nets. That rules
out conventional I2C (which needs pull-ups) and rules out any of these pads
being two probe points on one line.

**The unlabelled round pad is `VDD`.** It reads 10 ohm to the rail and its
forward diode figure (2.01 V) matches `VDD` (2.00 V). Not a ninth signal.

**`VPP` is unbonded.** Open in both directions, exactly like the `B` datum.
This weakens the Generalplus OTP-programming theory: a programming rail would
have to reach silicon.

**`SPL` is not SPI.** It sits beside `R9` and transistor `Q2` in the same
cluster as the battery terminals — a speaker driver. Read "SPL" as speaker.

## Resulting picture

A four-wire port:

```
CLK     clocked line
DATA    clocked line, likely bidirectional
RESET2  low-active reset
P0.12   unknown — candidate mode / test / boot-select
```

That shape fits three families of interface, all of which are *programming*
ports rather than runtime buses:

1. A proprietary clocked programming port gated by a mode pin.
2. SWD: `CLK`=SWCLK, `DATA`=SWDIO, `RESET2`=nRESET, `P0.12`=SWO or a spare.
3. An SPI-style ISP port with a single bidirectional data line.

**Operating assumption:** if this is a programming port, `CLK` and `DATA` are
silent during normal play. Silence is an expected result, not a fault.

## Next measurements

- `VPP` to `GND`, DC volts, powered. ~7 V = LCD charge pump (dead end).
- Forward diode with black on **`VDD`** instead of ground — isolates the
  pin-to-rail ESD diode and gives cleaner per-pin values.
- Static DC levels of `CLK` / `DATA` / `RESET2` / `P0.12` at idle.
- Same, with the battery removed and reinserted (does anything pulse?).

## Bench session 2026-11-08 — Pico 2 harness

Target powered from a bench PSU at 3.0 V into the battery holder, not a cell.
Pico 2 on USB. All target-side pins configured as inputs; `RESET2` pulled up.

### Baseline, target OFF

| Measurement | Value |
|---|---|
| `VDD` (ADC0) | 9 mV |
| `P0.12` | 0 |
| `CLK` | 0 |
| `DATA` | 0 |
| `RESET2` | 0 |
| CLK edges | 0 / s |
| DATA transitions | 0 / s |

The 9 mV is leakage on a floating rail, not a supply. Use as the "off" reference.

### Note on the earlier 542 mV reading

Before the harness was fully configured, ADC0 read 542 mV with the target off.
That was residual charge / differing pad state, not a rail. The configured
harness reads 9 mV, which is the number to trust.

## Bench session 2026-11-08 — interactive tests

Target on a bench PSU at 3.0 V into the battery holder. Pico 2 harness.
`RESET2` pulled up on the Pico side throughout.

### Powered baseline

| Measurement | Value |
|---|---|
| `VDD` (ADC0) | 3049 mV |
| `P0.12` | 1 |
| `CLK` | 1 |
| `DATA` | 1 |
| `RESET2` | 1 |
| CLK edges | 0 / s |
| DATA transitions | 0 / s |

Static current: brief 7 mA at switch-on, then below the PSU's resolution.
Consistent with an ASIC that animates a segment LCD and does nothing else.

### Line physics — both signal lines are open-drain

Control pins with nothing attached read `pulldn:0 pullup:1`. The harness pins
read `pulldn:1 pullup:1`, i.e. held high by something stronger than the Pico's
~80 kOhm pull-down.

Driving each line low as an output and reading it back returns 0, and the line
recovers to 1 when released. **A push-pull output would have fought back.** So
both `CLK` and `DATA` are open-drain with pull-ups. That is the physical layer
shared by I2C and SWD; it does not discriminate between them.

No cross-talk: toggling `CLK` leaves `DATA` at 1 and vice versa.

### I2C — negative

`SoftI2C` scan at 100 kHz, 400 kHz, 10 kHz and 1 kHz, with internal pull-ups
assisting: **no ACK at any address.**

An EEPROM on a powered bus would have answered. Note that the 2006 Connection
V3 does carry an HT24LC08 at 0xA0 on SDA/SCL, so the design precedent exists —
it just is not answering here.

### SWD — negative, and one discarded false positive

First attempt reported ACK `001` with `P0.12` held low, which looked like a
successful DP read. **It was an artifact:** the `1` was the last driven bit
still decaying on the line through the pull-down, sampled before it settled.
With a clean sample and a proper turnaround the ACK is consistently `111` —
an undriven line floating high. A valid read would return `001`.

**SWD is not answering.**

### Mode sweep on P0.12

`P0.12` held low, held high, and left floating, each followed by a reset pulse:
no ACK on SWD, no ACK on I2C, no data.

### Clocking the port

Driving 1024 clock edges immediately after releasing reset, at 20 us per half
period: `DATA` stayed high on every sample under both `P0.12` states.
`DATA` did not change on its own during 700 ms idle after reset.

### Power-cycle capture

60 s window, capture executed entirely on the Pico and written to its own
filesystem so a host disconnect could not break it. Operator cycled the PSU
several times.

```
window_ms     60000
rail_min_mv   812
rail_max_mv   3071
events        4
  21355  P0.12 0
  21355  P0.12 1
  40196  P0.12 0
  40196  P0.12 1
```

**`CLK` and `DATA` produced zero events across several power cycles.**

The four `P0.12` events are a low-going glitch pair at each of two power
transitions, most likely the rail sagging and recovering under a floating
input rather than the chip driving the pin.

### Capture bandwidth caveat

The polling loop ran at **31.5 k samples/s** with the ADC in the loop, 48.2 k
without. So this capture could see anything slower than roughly 2-3 kHz per
line. **A fast boot handshake in the hundreds of kHz or above would be
invisible to it.** This is a real limitation, not a detail — a logic analyzer
or PIO-based capture is required before "no boot traffic" can be called
conclusive.

## CORRECTION — RP2350-E9 invalidates the pull-down test

The RP2350 datasheet documents erratum **RP2350-E9** ("Increased leakage current
on Bank 0 GPIO when pad input is enabled"), affecting RP2350 A2 — which is what
this Pico 2 carries.

When a pad sits in the undefined logic region, it can source ~120 uA and hold
itself at **~2.2 V**. The internal pull-down (32-86 kOhm at 3.3 V) is weaker
than that leakage and **cannot pull the pad low**. Driving or pulling low with
8.2 kOhm or less does overcome it. The pad pull-up is unaffected.

Demonstrated on this board:

```
GP10  driven high then pull-down -> 1   (latch)
GP11  driven high then pull-down -> 1   (latch)
GP14  driven high then pull-down -> 1   (latch)
GP15  driven high then pull-down -> 1   (latch)
GP16  never driven, pull-down -> 0      (normal)
GP17  never driven, pull-down -> 0      (normal)
```

### What this retracts

The earlier claim "all four harness pins read `pulldn:1`, therefore they are
driven HIGH by the target" is **wrong**. Every harness pin sits at 3.3 V via the
board's pull-up, so enabling the internal pull-down dropped it into the
undefined region and the erratum latched it at 2.2 V. A pull-down test cannot
distinguish a driven-high pin from an E9 latch on this silicon.

### What still holds, and why

**Open-drain is still established.** We drove each line low as an output and
read back 0. An output driver is far below the 8.2 kOhm threshold, so it beats
the leakage. A push-pull target output would have fought back and read 1.
Therefore the target is **not** driving these lines; they idle high through
pull-ups.

**The I2C and SWD negatives still stand.** Both protocols acknowledge by pulling
the line *low*. A real target driver would win against the leakage. We read
all-ones, i.e. nothing drove low.

### Consequence for all further work

**Configure every input with PULL_UP, never PULL_DOWN, on this board.** The
errata states the pull-up is unaffected, and that "PIO programs can't toggle pad
controls," so an external or internal pull-up is required for reliable capture.

`probe.py` currently leaves CLK/DATA floating; that should become PULL_UP.

## PIO capture — full bandwidth, and both trigger paths validated

The polling capture above topped out near 2-3 kHz. To rule out a fast boot
handshake the whole exercise was repeated with PIO at the system clock.

### Instrument validation first

A capture that records nothing is indistinguishable from a broken capture, so
each sampling path was proven against a known-good signal before use. A second
PIO state machine generated a square wave into the line under test.

| Path | Self-test result |
|---|---|
| CLK-triggered SM | 1242 words in 100 ms, decoding `10 11 10 11 ...` (DATA high, CLK alternating) |
| DATA-triggered SM | 1243 words in 100 ms |

Both paths confirmed able to see edges. Neither reported an error on the live
target.

### Live capture across power cycles

40 s window, edge-triggered on CLK, PIO at 150 MHz, buffer on the Pico's own
filesystem:

```
seconds       40
words         0
rail_min_mv   857
rail_max_mv   3067
clk_idle 1  data_idle 1  p012 1  rst 1
```

`rail_min_mv 857` against `rail_max_mv 3067` proves the power cycles fell
inside the window. **Zero CLK edges.**

A DATA-triggered watch on the already-powered target returned zero over 8 s
idle, closing the "maybe it signals on DATA without clocking" gap.

### What this settles

There is **no traffic on CLK or DATA**, in normal operation or at power-up,
from DC to the limit of a 150 MHz PIO sampler. Combined with the earlier
protocol probes, every software-reachable hypothesis has been tested and
failed, each with an instrument that was verified working.

## The RESET2 trap — target dead whenever the Pico is plugged in

Symptom reported: the Tamagotchi will not power on while the Pico is connected
to the PC. The bench PSU reads 0 A. Unplug the Pico and the device boots
normally.

### Cause

RP2350 pads default to **pull-down** at power-up. `RESET2` is active low, so an
unconfigured GP4 asserts reset the moment the Pico enumerates and holds the
target there. The chip draws essentially nothing, which is why the supply reads
0 A rather than showing a fault.

This is the hazard documented in `wiring/pico2-pinout.md`, and it is not
theoretical — it took the device down in practice.

### Why an earlier reading was misleading

A check that read `RESET2` as `1` (released) used `Pin(4, Pin.IN)`, which
**reconfigures the pad** and clears the pull-down. The measurement itself
released the reset. The reported value was a property of the probe, not of the
board.

**Rule: never infer a pad's power-up state by reading it with a fresh `Pin()`
object — creating the object changes the state you are trying to observe.**

### Fix

`boot.py` on the Pico sets `PULL_UP` on GP4 first, before anything else:

```python
from machine import Pin
Pin(4, Pin.IN, Pin.PULL_UP)   # RESET2 released
Pin(2, Pin.IN, Pin.PULL_UP)   # CLK
Pin(3, Pin.IN, Pin.PULL_UP)   # DATA
Pin(5, Pin.IN, Pin.PULL_UP)   # P0.12
```

After a clean assert-then-release of `RESET2`, the rail read 2983 mV and both
`CLK` and `DATA` returned to 1 — the board's pull-ups alive, chip running.

### Alternative, if this keeps biting

Leave the `RESET2` lead disconnected except when a test needs it. Sniffing does
not require reset control.

## ADC measurement trap on GP26 — and the corrected reading

The `VDD` sense first read 1195 mV while the board was demonstrably running
(`CLK`/`DATA` both held high by the board's own pull-ups). That contradiction
was a measurement fault, not a wiring fault.

**Cause:** the pin's digital input buffer was left enabled. RP2350 requires the
GPIO input enable to be cleared when a pin is used as a SAR ADC channel; leaving
it enabled perturbs the sample. Toggling the pad through a pull-up/pull-down
test cleared it, after which the same pin read **3048 mV**.

**Proof the wire was always fine:** with the internal pull-down enabled, GP26
still read 1. A 36-113 kOhm pull-down cannot beat a real 3 V rail, so the pad
was held by a low-impedance source the whole time. It was connected; the ADC was
lying.

**Consequence:** the `rail_mv 1176-1224` line in an earlier capture log is an
artifact and must not be read as a rail measurement. The `857 - 3067` range in
the power-cycle log is valid — it spans a genuine off/on transition.

Fix applied: clear `IO_BANK0` function select for GP26 before sampling.

## FINAL capture — live device, LCD attached, egg animating

15 s, both trigger paths active simultaneously, 150 MHz PIO, ADC input enable
cleared:

```
CLK  samples : 0
DATA samples : 0
P0.12 chg    : 0
rail_mv      : 2967 - 3067
state        : CLK 1  DATA 1  P0.12 1  RST 1
```

**This is the definitive result.** The rail stayed between 2967 and 3067 mV for
the entire window, so the device was powered and working throughout — the
operator confirmed the egg was bouncing on the LCD at the time. Both lines idle
high, no transitions on either.

**There is no traffic on CLK or DATA during normal operation.** Combined with
the earlier boot captures, there is none at power-up either.

## AUTONOMOUS CAPTURE — three power cycles, zero bus traffic

Capture ran from `main.py` on the Pico itself, independent of the host, logging
one status line per second for 54 s. The operator power-cycled the Tamagotchi
three times while it ran. Device fully assembled, LCD attached, egg animating.

```
t_ms    rail_mv  clk_edges  data_edges  p012_changes
 999     3044        0          0            0
 7999    3052        0          0            0
 8999    2437        0          0            0     <- power off begins
11999    1028        0          0            0
12999    2983        0          0            2     <- power restored
15999    3050        0          0            2
16999    2781        0          0            2     <- power off begins
20999     921        0          0            2
21999    2988        0          0            4     <- power restored
30999    2957        0          0            4
31999    2250        0          0            4     <- power off begins
33999    1222        0          0            4
34999    2991        0          0           30     <- power restored
53999    3048        0          0           30
# end t_ms=54174 clk=0 data=0 p012=30
```

### Reading it

**Rail:** three unmistakable decay-and-recover cycles (3052 -> 1028 -> 2983,
3050 -> 921 -> 2988, 2957 -> 1222 -> 2991). Each one brackets a real target
power cycle, so the capture provably spanned the boot events.

**`CLK` edges: 0. `DATA` edges: 0.** Not one transition across three full power
cycles, including the boot windows. No `cap2.raw` file was created, because
there was no traffic to record.

**`P0.12`: 2 changes per cycle on the first two, 26 on the third.** Consistent
with the pad following the rail as it sags through the undefined region while
the chip is unpowered — not with the firmware driving it. The third cycle shows
a longer/slower decay, which would explain the larger count.

### Verdict

Combined with the live-device capture the night before (zero edges for 15 s
with the rail verified in a 100 mV band), the result is consistent and repeated:

**There is no traffic on `CLK` or `DATA`, at boot or in operation, from DC to
the limit of a 150 MHz PIO sampler, across three independent power cycles.**

## VPP — measured 0 V under power

With the Tamagotchi running from the bench supply, DC volts from `VPP` to `GND`:

```
VPP to GND, powered : 0 V
```

### What this rules out

**`VPP` is not an LCD bias charge pump.** A Generalplus-style `VPP` in that role
sits at roughly 7.2 V and drops only under load. The LCD was actively driving an
animation at the time, so the bias would have been live. 0 V is decisive against
it.

### What it leaves

1. **Unbonded test point** — consistent with the earlier diode readings, open in
   both directions to BN0, and with `B` behaving identically. A floating pad
   reads ~0 V on a 10 MOhm meter because nothing charges it.
2. **A dormant programming rail** — an OTP `VPP` pin would idle at 0 V and only
   rise when a fixture applies it. Also consistent.

**These two cannot be separated by a voltmeter.** Both give 0 V.

### The test that separates them

Add a wire from `VPP` to a spare Pico GPIO (GP6, pin 9), then enable the pad
pull-up:

- reads **1** -> nothing is holding the pad down: unbonded, or a high-impedance
  input
- reads **0** -> the pad is bonded and held low by something real

Pull-up is the correct direction: erratum RP2350-E9 defeats pull-downs but
leaves pull-ups working.
