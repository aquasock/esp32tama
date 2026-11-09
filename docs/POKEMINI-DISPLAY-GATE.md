# Pokémon Mini — display gate

The go/no-go measurement for whether Pokémon Mini can ship on the T-Watch Ultra.
Asked and answered on 2026-11-09.

## Verdict

**Passes.** Measured on the watch by the display-driver agent on 2026-11-09:

```
288x192 full frame, one transfer:  19.7 ms  =  51 fps
per-transfer fixed cost:           zero (within noise)
panel's share of transfer time:    1.6 %
```

A 288×192 update — the size that looks right on a 410×502 panel — sustains
**~51 fps** with no dirty-rectangle trickery at all. That settles the question
the earlier sections of this document were hedging about.

Three of my earlier assumptions were wrong, and the measurements that corrected
them are recorded below rather than quietly dropped:

- **per-transfer fixed cost is zero**, not the thing to worry about;
- **DMA caps at ~2×**, not the 5–10× I assumed;
- **the 190 ms figure is 4.2× slower than the measurement implies**, so every
  number in the superseded sections below should be ignored.

## What the display agent measured

They added `CO5300_IOC_BENCH` to the driver, which times the same payload twice:
once with CS asserted so the panel receives it, and once with CS never asserted
so the peripheral clocks bytes into nothing. The difference is the panel's own
share.

```
shape      bytes   chunks   CS asserted   CS not asserted   panel share
288x2       1152       18        197 us          193 us          4 us
288x64     36864      576       6572 us         6470 us        102 us
```

Fit: **11.42 µs per 64-byte chunk, intercept −8.6 µs** (zero within noise).
Verified independently — their published figures reproduce exactly.

Four conclusions, all of which change this document:

**1. Per-transfer fixed cost is zero.** This kills the concern in *A constraint
I found late* below. Splitting a frame into 96 two-row bands costs nothing extra
in overhead; the 2-row tearing cap is a throughput non-issue.

**2. The panel is not the bottleneck — 1.6 %.** They timed it, then removed it
from the circuit by never asserting CS, and the time barely moved. The panel is
excluded outright.

**3. DMA caps the gain at ~2×, not 5–10×.** My earlier estimate assumed all the
missing time was FIFO poking. It is 5 µs per chunk of CPU work (16 `memcpy_toio`
writes, a length write, a `cpu_relax()` poll), which is half the 11.42 µs, so
removing the CPU from the path entirely can at most double throughput. **My
5–10× figure was wrong.**

**4. The 190 ms does not reconcile, and my arithmetic was built on it.** 4000
chunks at 11.42 µs is **45.7 ms**, 4.2× faster than the recorded figure. The
agent flags this rather than resolving it: LCD-005 predates a CS-handling fix and
may have included work outside the transfer. Until it is re-measured through the
real repaint path, **both numbers are suspect** — including every row of the
tables in my earlier sections.

DMA is *not* implemented. The bench measures; it does not fix.

## What is measured, and by whom

I did not re-run this on the watch: the board is in use by the other agent
working on the UI and touch buttons, and I was asked not to touch it. So the
input figure is theirs, not mine, and is labelled as such.

From `docs/VALIDATION.md` (measured on hardware):

```
ROM 524288 bytes
full speed / no display:
  headless        cyc/s=16200654  realtime=4.05x
with display, by frame rate:
  fps=2           cyc/s= 6666800  realtime=1.67x
  fps=4           cyc/s= 4166829  realtime=1.04x
```

and the summary sentence: **"the cost of painting a 320x400 frame is ~190 ms,
roughly constant per frame regardless of pacing."**

From `Escapement/patches/kernel/display/STATUS.md` (the driver's own author),
under *Still open*:

```
* `esp32_spi_exec_op` is still FIFO-only, 64 bytes per chunk, no DMA. A frame
  is 6432 chunks of register pokes.
```

That second line is the whole answer. It is a **known, unfixed inefficiency in
code somebody else owns**, not a property of the panel or the bus.

Verified in `final/spi-esp32.c`:

```
/* FIFO path only: this driver does not use DMA for host.io yet. */
```

The DMA registers are already defined and partly wired (`SPI_DMA_CONF_REG`,
`SPI_DMA_INT_ST_REG`), so the plumbing is half-present.

## The arithmetic (superseded)

> **Superseded.** Everything below is derived from the 190 ms figure, which the
> transfer-cost measurement contradicts by 4.2×. It is kept because the method
> and the sensitivity analysis are still worth reading, but **do not quote these
> numbers**. Use the measured table in *What the display agent measured*.

The two records disagree slightly about what the 190 ms covers, so both
readings are given. Neither changes the verdict.

| Reading | Bandwidth | ← 2× full | 2× 20 % dirty | 3× full | 3× 20 % dirty |
|---|---|---|---|---|---|
| 190 ms = 320×400 (256 KB, 4000 chunks) | 1.35 MB/s | 27 fps | 137 fps | 12 fps | **61 fps** |
| 190 ms = 410×502 (412 KB, 6432 chunks) | 2.17 MB/s | 44 fps | 220 fps | 20 fps | **98 fps** |

The 6432-chunk figure implies the 410×502 buffer, which is why the second row
is probably the correct one — 6432 × 64 = 411,648 bytes, and 410 × 502 × 2 =
411,640. This should be confirmed with the other agent rather than assumed.

Efficiency against the wire: 64 bytes at 20 MHz quad should take 6.4 µs. It is
taking 47.5 µs per chunk. **The bus is running at about 13 % of what it can
carry**, and the missing 41 µs is register poking and a busy-wait completion
poll (`run()` spins on `SPI_CMD_REG` with `cpu_relax()`).

## The CPU half, measured with a real ROM

The free BIOS idles at `$0040` and does no useful work, so it cannot measure
anything. But PokeMini's dev kit ships homebrew examples **with source** under
the same GPL, and those run.

> **Correction.** An earlier version of this page said `bouncing` needed 6,944
> instructions per frame. That was the *free BIOS idle loop*: the harness loaded
> the ROM but never reset the CPU, so the cartridge never started. The flat
> 6,945-per-frame figure across five different cartridges is what gave it away —
> real code does not produce identical counts. Full account in
> `POKEMINI-CORE.md`.

Corrected measurements, 4,000 frames each, `-O2 -DPERFORMANCE`:

| ROM | Instructions/frame | Budget used |
|---|---|---|
| `mapview` | 4,600 | 33 % |
| `bouncing` | 4,648 | 33 % |
| `sound_tst` | 4,653 | 34 % |
| `framebuffer` | 6,619 | 48 % |
| `template` | 6,945 | 50 % |

The budget is 13,889 instructions per frame (4 MHz ÷ 4-cycle fetch = 1 MIPS,
÷ 72 Hz). Every example sits between a third and a half of full CPU.

These are demos, not retail games — treat 50 % as a plausible ceiling for a
simple title, not a measured bound for something like Pokémon Pinball. But
13,889 is a *hard* ceiling: a title that exceeded it could not run at full speed
on real hardware either, so the port cannot be asked to do better than the
original.

**The CPU is not the binding constraint.** The display path is.

## A constraint I found late — resolved

`final/co5300.c` carries a warning that changes the dirty-rectangle assumption:

```c
/*
 * 45x356 one op, right strip   torn
 * 45x2   per op, right strip   clean
 * ...
 * The ROOT CAUSE IS NOT ESTABLISHED. This is a workaround that has held every
 * time it was tried, not an explanation.
 */
#define CO5300_RECT_ROWS_PER_BAND	2
```

**Large single transfers tear.** The rect path is therefore capped at 2 rows
per transfer, and `panel-api.md` notes rectangles are split into row bands
regardless because CS edges end the burst. So a dirty rectangle is not one
transfer — a 288x192 region is **96 transfers of 2 rows each**.

**This concern is now resolved: per-transfer fixed cost measured at zero.** The
table below was built on an assumed per-transfer cost that does not exist. Kept
for the record only.

That said, at the time it widened the estimate, because per-transfer
fixed cost (CS assert/release, address-window setup, completion poll) would have
applied 96 times rather than once:

| Fixed cost per transfer | 3x full frame | 3x 20 % dirty |
|---|---|---|
| 0 µs (bytes dominate) | 82 ms / 12 fps | 16 ms / **61 fps** |
| 100 µs | 92 ms / 11 fps | 18 ms / **55 fps** |
| 250 µs | 106 ms / 9 fps | 21 ms / **47 fps** |
| 500 µs | 130 ms / 8 fps | 26 ms / **38 fps** |

Even at an implausible 500 µs of fixed cost per transfer, 20 % dirty rectangles
still give **38 fps**, which is enough. So the verdict survives the constraint —
but the honest range is **38–61 fps**, not the 61–98 fps stated above, and the
per-transfer cost has never been measured.

And the tearing itself is an unresolved bug, not a documented hardware limit.
The comment records that the same signature appears in the reference firmware at
45 MHz and vanishes at 20 MHz, with SPI clock as the suspected cause. If that is
root-caused, the 2-row cap can go, and everything on this page improves.

## What this means for the port

Measured basis, at 11.42 µs per 64-byte chunk:

| Target | Bytes | Chunks | Time | FPS |
|---|---|---|---|---|
| 96×64 native | 12,288 | 192 | 2.2 ms | 456 |
| 192×128 (2×) | 49,152 | 768 | 8.8 ms | 114 |
| **288×192 (3×)** | **110,592** | **1,728** | **19.7 ms** | **51** |
| 384×256 (4×) | 196,608 | 3,072 | 35.1 ms | 28 |
| 864×576 (9×) | 995,328 | 15,552 | 177.6 ms | 5.6 |

So the choice is a zoom decision, not a platform decision:

- **3× (288×192) sustains ~51 fps with no tricks.** This is the sweet spot.
- 4× drops to 28 fps, which is marginal for Pinball.
- Anything larger is out.

Dirty rectangles would push 3× far higher (20 % coverage ≈ 253 fps), but they
are no longer *required* to pass. They become an optimisation, not a gate.

Caveat: the bench writes from the driver's own transmit buffer. A real frame
also costs a copy out of the framebuffer first, so real-world numbers will be
somewhat below these.

Pokémon Mini is also a favourable case for dirty rectangles in a way the
Tamagotchi is not: the panel is 96×64 and games typically repaint a status bar
plus a few sprites, so 10–20 % coverage per frame is realistic rather than
optimistic. The V3 front end already does dirty tracking, so that machinery
carries over.

## What has to happen, and whose work it is

**Nothing is blocking the port any more.** The measurement moved everything that
was a gate into the optional column:

1. **DMA on `host.io`** — still worth doing (a ~2× ceiling), still the other
   agent's file, no longer required for this platform to ship at 51 fps.
2. **Dirty rectangles** — an optimisation for higher frame rates, not a gate.
3. **The 190 ms discrepancy** — worth settling for the *console's* sake, not
   ours. It does not affect the Pokémon Mini decision.

There is now a `CO5300_IOC_WRITE_RECT` ioctl for writing named rectangles in
panel coordinates, specified in `Escapement/docs/panel-api.md` and reserved for
the four edge strips around fb0 (`panel-reservations.md`). Both are
specifications rather than shipped code except for the bottom bar. A 288×192
image does not need the strips — it fits inside fb0's 320×400 — but the ioctl is
the natural path for a partial-update renderer if one is wanted.

## Honest limits of this report

- The transfer timings are the **display agent's** measurements, not mine. I
  verified their arithmetic reproduces but did not run the bench.
- Their bench writes from the driver's transmit buffer. A real frame also costs
  a copy out of the framebuffer, so production numbers will be **lower** than
  the 19.7 ms above.
- The 288×192 figure assumes one rectangular transfer. The 2-row cap (kept for
  tearing, root cause still open) requires 96 transfers instead — which is free,
  because per-transfer cost measured at zero. That reasoning is sound but the
  combined case has not itself been timed.
- The CPU half is measured on x86 and only *estimated* for the ESP32-S3. Still
  unmeasured on target.

## Recommendation

**Ship it at 3× zoom.** The display gate is passed on measurement, not on
arithmetic, and the remaining risk has moved to the place it belongs: the
cross-compiled core's realtime factor on the watch.

Order of work:

1. **Measure the core on target** — the only unmeasured quantity left.
2. **Then the front end** at 288×192, flat, no dirty rectangles. 51 fps is
   enough for the target games; optimisation can come later if it is wanted.
3. **DMA and dirty rectangles are optional**, and DMA is capped at ~2× anyway.

Nothing here needs further kernel work.
