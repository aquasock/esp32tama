# The BHI260AP as a compute oracle — small in, small out

Thinking note, 2026-11-09. The reframe: stop asking what can cross the bus, and
ask what *shape* of problem fits. Small payload in, long compute, small payload
out. Then the bus is irrelevant.

**This is the right instinct, and it is the only framing under which this chip
makes sense.** But the 1980s analogy inverts, and that inversion is the whole
story.

## The 1980s comparison, inverted

The instinct is: an 8087 next to a 6502. Send it numbers, it does the maths, you
get answers back.

The problem is the clock ratios go the wrong way:

```
1980s   host 1 MHz      coprocessor 8 MHz      coprocessor is  8x FASTER
2026    host 240 MHz    coprocessor 50 MHz     coprocessor is 4.8x SLOWER
```

A coprocessor was worth having because it was *faster*. This one is not. By raw
throughput, offloading anything to it makes that thing **4.8× slower**.

So the classic justification does not transfer. What replaces it is three
narrower ones, in order of strength.

## Reason 1 — energy (the real one, and smaller than I first thought)

```
ARC EM4   @ 50 MHz,  2.8 mA    ->   56 pC per operation
ESP32-S3  @240 MHz,   40 mA    ->  167 pC per operation
                                  3.0x in the ARC's favour
```

**Correction to my own earlier arithmetic:** I said 8.8× before checking. It is
**3.0×**. A fixed job costs about a third of the energy and takes 4.8× longer.

For a watch that matters, but it is a 3× lever, not a magic one. And it only
applies if the ESP32 genuinely powers down while the job runs — if you sit
polling the bus, you have eaten the entire saving.

## Reason 2 — parallelism, which is stronger than it first looks

Both ESP32-S3 cores are already spoken for on this board:

```
core 0   firmware, WiFi/BLE
core 1   Linux
```

There is no idle core. So a *third* engine that runs autonomously is genuinely
extra capacity rather than a reshuffle — **provided the work is independent**.
That is a real architectural argument, and it is the one that survives even when
energy does not matter (watch on charge, plugged in).

## Reason 3 — autonomy

The job keeps running across an ESP32 reboot, a Linux restart, or a suspend.
For anything long-running and idempotent, that is worth something.

## The metric that decides viability

```
send 32 B, receive 32 B  =  64 B round trip  =  6.4 ms of bus
```

Against that:

```
compute  0.01 s  ->     2x   not worth it
compute  0.1  s  ->    16x   marginal
compute  1    s  ->   156x   fine
compute  10   s  ->  1562x   fine
compute  60   s  ->  9375x   invisible
```

**Anything past about a second of compute pays for its own bus time a hundred
times over.** So the design rule is blunt: *if the answer is not worth waiting a
second for, do not offload it.*

## Workloads, sorted by whether the shape fits

The ratio depends almost entirely on **compute time**, not on the workload. What
separates good candidates is whether the *answer is small*.

**Fits well — long compute, tiny answer:**

- **Gradient descent / parameter search.** 256 B of seed in, 8 B of optimum out,
  ten seconds of work. Ratio ~380×. The archetype.
- **Monte Carlo estimation.** 48 B of parameters, 16 B of estimate, tens of
  seconds. Ratio ~3000×.
- **Key or nonce search.** 32 B in, 16 B out, thirty seconds. Ratio ~6000×.
  Brute force is the purest fit for this shape there is.
- **Curve / model fitting.** Data in, *coefficients* out — see below.
- **Iterative convergence** of any kind: root finding, constraint solving,
  relaxation methods.

**Fits badly, despite looking like a fit:**

- **A sine table.** This is the example given, and it inverts badly: the table is
  *large*. A 256-entry 16-bit table is 512 B each way, and at 10 KB/s that is
  100 ms of bus for something the ESP32 computes with a hardware FPU in
  nanoseconds after the first call. **The output is the wrong size.**

  The fix is the interesting part — see "emit coefficients, not tables" below.

- **FFT.** Streams. Wrong shape.
- **Anything streaming.** Wrong shape by definition.
- **Anything the ESP32 does in microseconds.** A biquad design is 0.05 s of
  compute against 6 ms of bus — 8×, which does not clear the bar.

## The one genuinely elegant idea: emit coefficients, not tables

This is where the sine-table instinct becomes correct. Instead of asking for a
table, ask for the **smallest object that makes the ESP32 permanently faster**.

```
send    20 sample points (80 B)
receive 4 cubic coefficients (16 B)
```

The ESP32 then evaluates a cubic with three `madd.s` instructions at 240 MHz —
about 12 ns — forever, with no bus traffic at all.

**Being honest about what that wins, though:** a table lookup is also
nanoseconds. Two loads and a convert is not measurably slower than three fused
multiply-adds. The sinc of the gain is **not speed** — it is that the ESP32 no
longer needs the 512-byte table in flash, and never has to ask for it. On a board
where flash and bus are the scarce things and arithmetic is nearly free, moving
work from "data" to "a few coefficients" is the correct direction. But it is a
footprint win, not a throughput win.

That is the correct inversion of the 1980s idea. The coprocessor is not the
fast thing you call for every operation — it is the thing you call **once** to
produce something that makes the host fast.

Generalised: **use it as a compiler, not as an ALU.**

- Fit a curve → get polynomial coefficients
- Characterise a sensor → get a calibration polynomial
- Measure a thermal/battery response → get a model's parameters
- Tune a controller → get three gains
- Find the shape of a cost function → get a handful of knots

All small outputs. All permanent wins. All done once, at 3 a.m. on the charger,
where the 4.8× slowdown is irrelevant because nothing is waiting.

## The thing to check before any of this

Worth saying once and then leaving alone: **the chip is running Bosch's
firmware.** Custom code needs their SDK and a signed user payload. Every idea
above assumes you can load code onto it.

If you cannot, then the honest fallback is the one from the earlier note: use
the stock firmware for wake-on-motion and sensor batching, which is a real
battery win and needs no SDK at all. **That is not as interesting, and it is
much more likely to actually happen.**

## What I would actually build, if the SDK materialises

**A nightly curve-fitter.** The ESP32 collects a few dozen observations —
battery voltage against state of charge, thermal rise against load, a sensor's
non-linearity — posts them across the bus before sleeping, and the ARC core fits
a polynomial while the watch charges. Mornings, the ESP32 reads back four
coefficients and has a better model than it could afford to fit itself.

It is unglamorous. It is also the only shape on this list that turns an
otherwise idle chip into something the watch is measurably better for, and it
costs 96 bytes of bus traffic a day.

## Summary

| Question | Answer |
|---|---|
| Is the shape right? | **Yes.** Long compute, small answer, is exactly what fits. |
| Is it faster? | **No.** 4.8× slower. Do not offload for speed. |
| Is it cheaper? | **3× per operation** — real, not magic. |
| Is it extra capacity? | **Yes** — both ESP32 cores are already busy. |
| Does a sine table fit? | **No** — output too big. Emit coefficients instead. |
| What is the rule? | *If the answer is not worth a second's wait, do not offload it.* |
| What is the best idea here? | Use it as a **compiler**, not an **ALU**. |
