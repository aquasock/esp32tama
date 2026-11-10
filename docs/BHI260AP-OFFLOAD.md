# BHI260AP as an offload engine — what the bus allows

Analysis, 2026-11-09. The question: treat the BHI260AP as a black box you can
send numbers to over I2C. What can it usefully do that the ESP32-S3 could offload
to it — FPU, APU, GPU, PPU?

**Short answer: none of those. The bus forecloses all four, and the ESP32-S3
already has dedicated silicon for three of them.** What the chip *is* good for is
narrower and more useful than any of those labels.

All figures below are arithmetic from the datasheet and this project's measured
bus, not measurements on the part. Nothing was run on the watch.

## The number that decides everything

The BHI260AP sits on a **shared, bit-banged, 100 kHz I2C bus** — alongside the
PMU, touch controller, RTC, expander and haptic driver. That is not a fast link
between a CPU and a coprocessor. It is a slow one.

```
bit time          10 us
byte (8 + ack)    90 us
raw ceiling       11,111 B/s
sustained          ~10,000 B/s   (allowing for start/stop/addressing)
```

Ten kilobytes per second. For scale: that is **1/17th of a floppy disk per
second**, and about 1/1000th of what the ESP32-S3's own PSRAM delivers.

At that rate:

```
fp32    2,500 values/sec
int16   5,000 values/sec
```

And that is *one direction*. A closed loop that sends state and receives a result
halves it.

## Against the chip's own capability

```
BHI260AP ARC EM4 @ 50 MHz    180 CoreMark   ~tens of Mops/s
BHI260AP ARC EM4 @ 20 MHz     72 CoreMark
ESP32-S3 LX7 @ 240 MHz       840-960 CoreMark  (one core, of two)
```

So the co-processor is roughly **one fifth of one ESP32 core**. It is not a
bigger engine you are reaching for; it is a smaller, far more efficient one.

Do the division and you get the uncomfortable figure:

```
bus-reachable throughput   ~25 kops/s   (2,500 values/s x ~10 ops each)
core capability            ~50 Mops/s
                          = about 0.1 % of the chip
```

**Any workload you ship across that bus uses one part in a thousand of the
hardware.** The other 999 parts sit idle waiting for bytes.

## The four questions, answered

### Can it be an FPU? No.

The ARC EM4 has a hardware FPU — single precision, one op per cycle at 50 MHz.
But:

- The **ESP32-S3 already has a hardware single-precision FPU** at 240 MHz. It is
  roughly 5× faster than the ARC core for the same work, and needs no bus.
- To use the BHI260AP as an FPU you would ship operands 4 bytes at a time over a
  link that carries 10 KB/s. Realistic throughput: ~2,500 FLOP/s round-tripped,
  against ~50 MFLOPS local.

**You would be making floating point about 20,000× slower** — to a part that is
five times slower per clock and has a quarter of the clock. There is no version
of this that is worth doing.

### Can it be an APU? No.

Two independent reasons:

- **Bandwidth.** Audio is streaming and continuous. 44.1 kHz stereo 16-bit is
  **176 kB/s** — 17× the entire bus. Mono 16-bit at 44.1 kHz still needs 88 kB/s.
  The bus cannot carry a single audio channel at music rates. It carries 5.5 kHz
  mono at a stretch.
- **No output path.** The BHI260AP is a sensor hub. It has no DAC and no audio
  interface. Results leave over the same slow I2C.

The ESP32-S3 has I2S plus two DACs. Audio offload in this direction makes no
sense at all.

### Can it be a GPU or PPU? No.

```
96x64 mono 1bpp     768 B/frame  ->  0.1 s/frame
288x192 RGB565  110,592 B/frame  ->  11.1 s/frame
```

Eleven seconds per frame for a 3×-zoom Pokémon Mini image. The bus is 1/700th of
what a 60 fps framebuffer would need.

Also: the ESP32-S3 has a **PPA** (2D pixel-processing accelerator) and LCD_CAM on
die. Those exist precisely for this and do not need a bus.

There is one exotic escape: the chip has **three master interfaces**, so it could
in principle drive a peripheral *directly*, bypassing the ESP32. That is I/O
offload, not compute offload, and there is no data path from it to this watch's
panel (which is on SPI2 under the ESP32's control).

### So what is it for?

Not "offload work to". **"Replace the host while the host sleeps."**

That is the actual design premise, and the datasheet says so: the chip exists to
run *always-on* processing at 950 µA so the main processor can stay off.

The one thing that makes it work is that **the sensor is inside the chip.** The
6-axis IMU is on the same die. So for sensor-driven work, **the input side costs
nothing on the bus.** That inverts the whole calculus:

| Pattern | Traffic | Verdict |
|---|---|---|
| Ship data in over I2C, get results back | up to 10 KB/s both ways | ~0.1 % of the chip used |
| Data originates on-chip (IMU), only results leave | 16 B per result | **the design it was built for** |

Concretely, closed-loop offload over the bus is viable only at low rates:

```
10 Hz,  4 fp32 state vars, round trip     3.2 % of bus   comfortable
50 Hz,  4 fp32 state vars                16.0 % of bus   workable
100 Hz, 4 fp32 state vars                32.0 % of bus   already competing with touch
100 Hz, 16 fp32 in + out                128.0 % of bus   impossible
```

And sensor-side, where input is free:

```
1 result/sec    (16 B)     0.16 % of bus
50 results/sec  (16 B)     8.0  % of bus
200 results/sec (16 B)    32.0  % of bus
```

## The honest pitch

**Not performance. Power.**

```
BHI260AP   64 CoreMark/mA   (2.8 mA turbo)
ESP32-S3   22 CoreMark/mA   (~40 mA active)
           ~2.9x better in the BHI's favour
```

That is the whole value proposition: **about 3× the compute per milliamp**, in a
package that can run while the ESP32 is in deep sleep. It is not a faster engine.
It is a cheaper one, in the only currency that matters on a battery watch.

So the useful framing is not *"what can I offload to make things faster"* — the
bus guarantees it will be slower — but *"what can run entirely over there so this
processor can stop."*

## What that points at, concretely

Work that fits the one viable shape — **data from the on-chip IMU, tiny results
out, latency tolerated**:

1. **Wake-on-gesture or wake-on-motion.** The ESP32 sleeps; the ARC core watches
   the accelerometer continuously at 950 µA and raises an interrupt on a
   threshold. The ESP32 wakes in microseconds. This is the killer app for this
   part and needs no custom firmware at all if Bosch's stock algorithms cover it.
2. **Always-on activity classification** — steps, still, walking, cycling, logged
   on-chip and read once a minute.
3. **Anomaly or threshold detection** on vibration, for a maintenance or
   tamper-detection use.
4. **A low-rate control loop** (≤50 Hz, a handful of state variables) that must
   keep running while Linux is suspended.

Work that does not fit, and should not be attempted:

- Any FPU, DSP, audio or graphics offload.
- Anything needing more than ~10 KB/s in either direction.
- Anything latency-sensitive: a round trip over I2C at 100 kHz, competing with a
  touch controller, is **milliseconds**, not microseconds.

## If you want to push the bus anyway

Worth noting because it changes one of the constraints: **the BHI260AP supports
SPI up to 50 MHz** on its host interface (I2C is the fallback). On this board it
is wired for I2C at 100 kHz, but the part is not the limit — the wiring is.

If a future board revision put that chip on SPI, the arithmetic above changes
completely: 50 MHz quad SPI is ~25 MB/s, which is 2,500× the current bus and
makes real offload plausible. **On this board, it is not.**

The other lever is the bus itself: it is **bit-banged** on GPIO 3/2, not a
hardware I2C peripheral. That is already a known throughput limit in this
project. Moving to hardware I2C and raising the clock to 400 kHz (the part's
ceiling is 3.4 MHz) would be a 4× to 34× improvement — still nowhere near enough
for graphics or audio, but enough to make 100 Hz control loops comfortable.

## Verdict

| Asked | Answer |
|---|---|
| FPU | **No.** ESP32-S3 has one 5× faster, no bus needed. Would be 20,000× slower. |
| APU | **No.** Needs 17× the entire bus, and the part has no audio output. |
| GPU / PPU | **No.** 11 s per 3×-zoom frame. ESP32-S3 has a PPA already. |
| General offload | **No, on this bus.** You can reach 0.1 % of the chip. |
| Always-on co-processor | **Yes** — and this is what it was designed for. 3× perf/watt, runs while the ESP32 sleeps. |

**So: not an accelerator. A duty-cycle extender.** Its value is measured in
battery, not in throughput, and the moment you have to feed it across that
100 kHz bus you have already lost.

The two things worth investigating, in order:

1. **Does Bosch's stock firmware already do wake-on-motion?** If yes, this is a
   battery win available today with no SDK, no ARC toolchain and no custom
   firmware — just I2C register writes to enable an existing feature.
2. **Would Bosch give you the SDK?** Only relevant if answer 1 is no and you need
   custom on-chip logic. That is the gate, and it is a commercial question before
   it is an engineering one.

---

# Addendum: would max I2C speed change anything?

Asked directly, and the answer is **no — not for the IMU, and not in a way that
helps.** The reasoning is worth writing down because it closes the question
rather than leaving it hanging.

## The IMU was never bandwidth-limited

An IMU produces very little data. 6-axis at 12 bytes a sample:

```
 50 Hz     600 B/s     6.0 % of a 100 kHz bus    0.18 % of 3.4 MHz
100 Hz   1,200 B/s    12.0 %                            0.35 %
200 Hz   2,400 B/s    24.0 %                            0.71 %
400 Hz   4,800 B/s    48.0 %                            1.41 %
1600 Hz 19,200 B/s   192.0 %  <- the BMI's maximum rate, and only 5.7 % of 3.4 MHz
```

So even at the sensor's top speed, a 3.4 MHz bus would be 94 % idle. **At any
sane rate, 100 kHz was never the constraint.** Raising the clock would buy
nothing for reading the IMU.

## The compute side is even less of a constraint

Which makes the whole premise worth checking. A tap/gesture detector is roughly:

```
compare 3 axes to a threshold        6 ops
debounce counter update              3 ops
state machine transition             5 ops
                                  -------
per sample                          15 ops  ->  3,000 ops/s at 200 Hz
```

Against that:

```
ESP32-S3   240 M ops/s   0.0013 % used
ARC EM4     50 M ops/s   0.006  % used
a 1 MHz 6502  1 M ops/s  0.3    % used
```

**Your instinct that a 6502 could do this is correct, with room to spare.** At
200 Hz a 1 MHz 6502 uses three tenths of one percent of its cycles. The ARC core
is roughly fifty times more capable than needed, and the bus is roughly ten times
more capable than needed, for this particular job.

Which reframes the chip entirely: **it is not short of capability. It is short of
a reason to exist on this board.** Its only real advantage is that it runs on
950 µA while the ESP32 sleeps — a power argument, never a performance one.

## What a faster bus *would* change — and why it does not help

At the part's I2C maximum of 3.4 MHz (Fast-mode Plus), the two workloads that
were impossible become possible:

```
audio 44.1 kHz stereo   176,400 B/s   52 % of 3.4 MHz   fits
frame 288x192 RGB565    110,592 B/s   33 % of 3.4 MHz   fits
```

But those are exactly the cases where the ARC core is too small to be worth
using:

- The chip has **no audio output** and no DAC. Where would the audio go?
- The chip has **no path to this watch's panel**, which is on SPI2 under the
  ESP32's control. Where would the pixels go?
- And the ESP32-S3 already has an **I2S/DAC** for audio and a **PPA** for pixels,
  both faster than the ARC core and needing no bus at all.

So the faster bus unlocks only workloads with no output path, on a host that
already does them better. **It is not a real unlock.**

## Three nested ceilings, and the real one is the first

Worth distinguishing, because "100 kHz" is not the silicon's fault:

```
BHI260AP I2C maximum        3.4 MHz    the part is not the limit
ESP32-S3 hardware I2C max   ~1 MHz     the SoC's peripheral
bit-banged i2c-gpio         ~100-400 kHz  what this board actually uses
```

**This board bit-bangs the bus on GPIO 3/2** (`compatible = "i2c-gpio"`), because
there is no hardware I2C controller driver for the ESP32 in this kernel tree —
`drivers/i2c/busses/` has no ESP32 entry at all. So the 100 kHz is a software
choice driven by a missing driver, not a hardware property.

In principle that is fixable in two steps, neither of which helps this use case:

1. **Hardware I2C** — write an ESP32-S3 I2C controller driver (~1 MHz). Removes
   the bit-bang overhead entirely.
2. **Raise the clock** — 400 kHz is trivial for hardware I2C, 1 MHz achievable.

Even both together give 10× on a link that is 94 % idle for IMU traffic. The gain
would show up in **touch responsiveness**, not in anything the IMU does.

## Conclusion

| Question | Answer |
|---|---|
| Would max I2C let the IMU do more? | **No.** The IMU is 0.35 % of a 3.4 MHz bus. |
| Would it make offload viable? | Only for audio and pixels, which have no output path and are already done better on-SoC. |
| Is 100 kHz a hardware limit? | **No.** It is a bit-banged bus because no hardware I2C driver exists in this kernel. |
| Is the bit-bang worth fixing? | **For touch latency, yes.** For the BHI260AP, no. |

The frustration is fair, but it is aimed at the wrong thing. The bus is not
wasting the chip — **the chip's capability was never the point.** It was put on
this board to run for weeks on a coin cell's worth of current, and on that
measure it is doing exactly what it was bought for. Everything it *could* be
repurposed into is either already done better on the ESP32, or blocked by the
signed-firmware requirement rather than by I2C.
