# Pokémon Mini — display gate

The go/no-go measurement for whether Pokémon Mini can ship on the T-Watch Ultra.
Asked and answered on 2026-11-09.

## Verdict

**Passes, conditionally.** The display path is not a hardware wall — it is a
driver implementation limit that is already documented by the agent who owns
that code, and it has roughly **7–13× of headroom** available.

Pokémon Mini at 3× zoom with dirty-rectangle updates lands at **61–98 fps**,
which is comfortably enough for the target games. Full-frame at 3× does not,
at 12–20 fps.

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

## The arithmetic

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

The free BIOS idles, so it cannot measure real work. But the PokeMini dev kit
ships homebrew examples **with source** under the same GPL — a legal test ROM
that actually renders. Using `dev-misc/examples/bouncing/bouncing.min`:

```
loaded 11968 bytes
frames=300
CPU steps/frame = 6944   (budget at 1 MIPS / 72 fps = 13889)
steps/sec = 58,088,700
```

So a real homebrew title needs **6,944 instructions per frame against a budget
of 13,889 — about 50 % of the CPU**. In absolute terms that is 500,000
steps/sec, not 1 MIPS.

Caveat: `bouncing` is a simple demo with a background and one sprite. A retail
game such as Pokémon Pinball will be heavier. Treat 50 % as a floor on usage
rather than a typical figure.

Read against the ESP32-S3 at an assumed 150–200 host cycles per emulated
instruction (240 MHz ÷ 200 … 150 = 1.2–1.6 M steps/sec), that is
**2.4–3.2× realtime for this ROM**. The CPU side is not the problem.

## What this means for the port

At 3× zoom (288×192, the size that actually looks right on a 410×502 panel):

- **Full frame, today's driver:** 12–20 fps. Not shippable.
- **20 % dirty rectangles, today's driver:** 61–98 fps. Shippable.
- **Full frame, with DMA:** 90–150 fps. Plenty.

Pokémon Mini is also a favourable case for dirty rectangles in a way the
Tamagotchi is not: the panel is 96×64 and games typically repaint a status bar
plus a few sprites, so 10–20 % coverage per frame is realistic rather than
optimistic. The V3 front end already does dirty tracking, so that machinery
carries over.

## What has to happen, and whose work it is

1. **DMA on the `host.io` path** — `esp32_spi_exec_op` in
   `Escapement/patches/kernel/display/final/spi-esp32.c`. This is the other
   agent's file. Expected 7–13× improvement; it is the single highest-value
   change for any emulator on this board, Pokémon Mini included.
2. **Partial blit** — `co5300_blit` currently sends a whole window as one
   spi-mem op, and STATUS notes there is "no double buffering, no partial
   blit". Needed for dirty rectangles.
3. **Confirm the 190 ms basis** — whether it was 320×400 or the full 410×502
   buffer. Cheap question, changes the estimate by 1.6×.

Items 1 and 3 are askable in one message. Item 2 is port-side work.

## Honest limits of this report

- The 190 ms is **their measurement, not re-verified by me**. I was asked not
  to touch the watch while the other agent is on it.
- The DMA and dirty-rectangle columns are **arithmetic on measured bandwidth**,
  not measurements. They are the right order of magnitude and nothing more.
- The 20 % dirty figure is an assumption about game behaviour, not a
  measurement of Pokémon Mini specifically.
- So the gate is a **strong pass on paper and still needs one hardware
  confirmation**: either a DMA'd frame time, or a real dirty-rectangle blit
  timing. That confirmation is cheap once the board is free.

## Recommendation

Proceed, but in the order that respects the risk:

1. Ask for DMA + a re-measure. That is one message and it converts the whole
   table from arithmetic into measurement.
2. While waiting, do the part that needs no display at all: vendor PokeMini,
   build the headless harness, and measure the core's realtime factor. That
   work is independent, reversible, and useful even if the display verdict
   flips.
3. Only wire UI and rendering after both numbers exist.
