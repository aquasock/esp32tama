# BHI260AP — every feature you already own

Researched 2026-11-09 from Bosch's datasheet (`BST-BHI260AP-DS000` rev 1.1).
**Nothing measured on the watch.**

This changes my earlier advice. I told you to turn the IMU off, on the basis that
it was only good for detecting movement. **The datasheet has a table I had not
read, and it says otherwise.** Several features cost under 100 µA — two orders of
magnitude below the 950 µA figure I had been quoting, which is the CPU running
flat out rather than an event-driven algorithm.

## What it costs per feature — the table that matters

Bosch publishes per-virtual-sensor current, and it is the thing that decides
whether any of this is worth having:

| Feature | µA | mAh/day | mAh/week |
|---|---|---|---|
| **Tilt** | **37** | 0.89 | 6.2 |
| **Significant motion** | **37** | 0.89 | 6.2 |
| **Glance gesture** | **94** | 2.26 | 15.8 |
| **Pickup gesture** | **94** | 2.26 | 15.8 |
| **Step detector** | 98 | 2.35 | 16.5 |
| **Step counter** | 98 | 2.35 | 16.5 |
| **Activity recognition** | 98 | 2.35 | 16.5 |
| Wakeup gesture | 261 | 6.26 | 43.8 |
| Game rotation vector @25 Hz | 1,068 | 25.6 | 179 |
| Rotation vector (9DoF) @25 Hz | 1,126 | 26.3 | 184 |

All at Long Run (20 MHz), which is the mode these run in. Turbo (50 MHz) costs
only 4 % more for the event-driven features — 0.102 mA against 0.098 for step
counting — because they sleep between events and never need the clock.

For scale: **ESP32-S3 deep sleep is about 10–20 µA** (0.24–0.48 mAh/day). So the
cheapest useful features here cost roughly 2–4× what sleeping costs, and the
expensive ones cost 50–100×.

Your battery is, in your words, a monster. **Tilt plus glance is 131 µA = 3.1
mAh/day.** That is the whole decision, and it is small.

## The features, and which are real

### Confirmed in the datasheet with a power figure

These are named in Bosch's own power table, so they exist in the stock firmware
and are reachable without any SDK:

**Tilt — 37 µA.** Reports when the device is tilted past a threshold. The
cheapest useful thing on the chip.

**Significant motion — 37 µA.** Fires once when movement starts after a period of
stillness. This is the classic "is it being carried" trigger, and at 37 µA it is
the right primitive for waking something else.

**Glance gesture — 94 µA.** Bosch's name for wrist-raise. **This is the one that
matters for a watch.** Lift your wrist, the sensor says so, the screen comes on.
Every smartwatch does this; this is the hardware that makes it possible, already
fitted and already paid for.

**Pickup gesture — 94 µA.** Distinguishes lifting the watch off a table from
lifting your wrist. Useful for deciding whether to light up.

**Step detector — 98 µA**, and **step counter — 98 µA**. Detector fires an event
per step; counter maintains a running total internally, so you can read it once a
minute instead of once a step. The counter is the better of the two for battery
and I would use it.

**Activity recognition — 98 µA.** See below; this one has more resolution than
the name suggests.

**Wakeup gesture — 261 µA.** Trainable wake gesture. Pricier; probably not worth
it when glance is 94 µA.

### Confirmed in the datasheet, no power figure given

**Self-learning AI** — the genuinely surprising one. See the next section.

**Pedestrian dead reckoning (PDR)** — position X/Y, heading plus heading accuracy,
and step count, in one FIFO event. This is indoor positioning from the IMU alone:
it estimates *how far you walked and in which direction*. Ordinarily you would
pair this with GNSS and let it fill the gaps.

**Activity classes.** The bitmap is explicit: **Still, Walking, Running, On
Bicycle, In Vehicle** — five classes, each with an "ended" event. That is much
more useful than "moving / not moving".

**BSX fusion** — quaternion, Euler and 3D-vector orientation outputs, 6-axis and
9-axis (9-axis needs an external magnetometer, which this board does not have).

**Swim analytics** — stroke detection and lap counting. Useless on a watch you
wear in the shower at most, but it is in there.

**Raw accelerometer and gyroscope** streaming to 1,600 Hz, with FIFO batching.

### Implied by "all Android CDD sensors and beyond", not individually confirmed

Bosch states the chip supports **all virtual sensors defined in the Android
Compatibility Definition Document**. That implies the standard set also present —
gravity, linear acceleration, rotation vector, orientation, and so on — but the
datasheet does not enumerate them, and I have not verified each one on this part.

**The way to settle it is one command: `Virtual Sensors Present (0x011F)`.** It
returns a bitmap of exactly what this firmware on this device supports. That is a
couple of I2C reads and needs no SDK, no driver and no firmware. It is the single
most useful thing to run first when the board is free.

## The self-learning AI is a real API, and needs no SDK

This is the finding I would not have predicted. The gesture-learning feature is
exposed as **documented host parameters** — section 13.3.6 — not as something
buried in the SDK:

| Parameter | ID | Access |
|---|---|---|
| Learning and recognition state | `0x900` | Read/Write |
| Read learnt pattern | `0x901` | Read |
| Write pattern for recognition | `0x901` | Write |
| Algorithm parameters | `0x902` | Read/Write |
| Pattern state operation | `0x904` | Write |
| Pattern similarity calculation | `0x905` | Read/Write |
| Driver status | `0x907` | Read |

The workflow, entirely from the host:

1. Enable learning (`0x900`).
2. **Do the gesture.** The chip signals when it has learned a pattern.
3. Read the pattern out (`0x901`) — it is a small blob, with a size field and an
   ID.
4. Store it, or write patterns back in for recognition.
5. Enable recognition. When the gesture recurs, you get an event.

There is also a similarity score: write two patterns, read back an IEEE-754 float
where **1.0 means very similar** and negative means different. Bosch's own helper
is called `bhy2_klio_similarity_score()`, which is the honest family name for
this kind of technique.

So: **you can teach this watch a custom gesture, from the host, with no SDK and no
signed firmware.** The limitation is that the patterns are the chip's own fixed
encoding — you cannot change the algorithm, only what it recognises. But you do
not need to.

Twist your wrist twice to dismiss a notification. Draw a circle to open the
stopwatch. Whatever you want. It costs the same 94 µA class as glance, because it
is the same event-driven machinery.

## How the power actually works

Worth understanding, because it explains the 37 µA figures:

> "it is possible that the Fuser2 remains in sleep mode, while the IMU remains
> active, waiting for movement in order to wake-up the Fuser2 core."

The IMU samples continuously at very low power; the **CPU stays asleep** and is
woken only when a threshold is crossed. Firmware manages the power states itself —
the datasheet is explicit that "there is no need for a user to interact with them
directly."

That is why an always-on feature costs 37 µA while the same core running CoreMark
costs 950 µA. **The expensive number I quoted earlier was the wrong number**, and
I should have read this table the first time.

## Which ones I would actually enable on this watch

Ranked by "watch feels better per microamp":

1. **Glance gesture (94 µA).** Wrist-raise-to-wake. This is the single biggest
   quality-of-life win available on the whole board, and it is a register write.
2. **Step counter (98 µA).** Read once a minute, not per step. A watch that counts
   steps is expected to count steps.
3. **Tilt (37 µA).** Cheapest thing here; pairs with glance for orientation
   decisions.
4. **Self-learning AI (94 µA class).** Custom gestures, host-taught, no SDK. The
   interesting one.
5. **Activity recognition (98 µA).** Only if you want to show walking/running/
   cycling. Five classes is enough to be a real feature.
6. **PDR.** No power figure published; likely the most expensive of the useful
   ones, and the least trustworthy indoors. Try it, do not build on it.

All six together would be well under 600 µA if they can run concurrently, but
they probably cannot cheaply — several want the accelerometer at different rates.
**Start with glance and step count at 192 µA = 4.6 mAh/day**, and add from there.

## What I got wrong

I told you to switch the IMU off because it "only detects movement". That was
based on the 950 µA CPU figure and a reading of the feature list that stopped at
"step counting".

The corrections, for the record:

- **37–98 µA, not 950 µA**, for the event-driven features. I quoted the
  CPU-saturated number as if it were the always-on number.
- **Glance and pickup gestures exist** — wrist-raise is in the stock firmware,
  and it is the feature that most makes a watch feel like a watch.
- **The self-learning gesture API is host-accessible**, so custom gestures need
  no SDK after all. That was the one thing I said was gated behind Bosch, and for
  *this* feature it is not.

The SDK is still needed for genuinely new algorithms. It is not needed for
anything on this page.

## What to do first

**One command settles most of it: read `Virtual Sensors Present (0x011F)`.** It
returns the bitmap of what this exact firmware supports. Everything above that is
marked "confirmed" should appear in it; anything implied by the Android CDD will
show up or not.

That is a handful of I2C reads — no driver, no SDK, no firmware, no risk. It is
the first thing to run when the board is free.

---

# Addendum: correcting the "fitted motion patch" model

The mental model was: *it takes a sequence of x/y/z, learns it, and spits back an
ideal fitted motion patch.*

Three parts of that are wrong, and the corrections are more useful than the idea.

## Correction 1: cyclic only

> "The self-learning AI software can be used to learn new **cyclic gestures**...
> The cyclic gesture needs to be the same in between the repetitions and the
> device needs to **come to the original position to close one cycle**."
> — §7.4

It does not learn a sequence. It learns a **loop**. A line, a triangle, a
one-directional swipe, a "draw a Z" — none of those are learnable. A circle, a
wrist twist-and-return, a bicep curl, a squat, a shake — those are.

Bosch's own examples are exercise shapes: **bicep curls, triceps extensions,
squats.**

## Correction 2: it is a rep counter, not a shaper

What comes back is not a fitted trajectory. It is:

- **which pattern matched** (or "no match at all" — the datasheet is explicit
  that it handles the negative case),
- **a repetition count**, updated in real time,
- and, separately and only if you ask for it, **the learnt pattern blob** — a
  compact parameter set, not a curve.

> "The self-learning algorithm **estimates a number of parameters**, like the
> **frequency and phase of some components** of the movement."
> — §7.4

So the internal representation is an oscillator model — frequency and phase per
component. It is a **descriptor**, in the same family as the polynomial
coefficients idea from the earlier note: small, compact, not a replay of the
data.

It does **not** smooth, interpolate, generate, or reconstruct motion. If you want
a fitted trajectory you fit it yourself; this chip tells you *which* loop you are
doing and *how many times*.

## Correction 3: the speed window is narrow

**0.3 to 1.8 Hz** — one cycle every **0.56 s to 3.33 s**.

```
0.3 Hz   3.33 s/rep   18 reps/min   slow and deliberate
0.5 Hz   2.00 s/rep   30 reps/min   exercise tempo
1.0 Hz   1.00 s/rep   60 reps/min   brisk
1.8 Hz   0.56 s/rep  108 reps/min   fast
```

Outside that window, nothing. That rules out anything twitchy, anything slow and
drift-y, and anything faster than about two per second.

## And the point that decides the whole "feed it numbers" idea

You cannot feed it numbers, unless you have the SDK.

Sensor data injection exists — command `0x0007`, with a companion `Inject Sensor
Data` at `0x0008` — but:

> "A **dedicated firmware version is required** for this, which replaces the
> standard physical sensor drivers with data injection drivers. **This can be
> compiled using the SDK.**"

So the black-box-pure model is gated behind exactly the thing that gates
everything else. **Synthetic input needs the SDK.**

But teaching it a real motion does not:

- **Move the watch** through the loop. That is real IMU data from the on-chip
  sensor. No SDK, no injection, no firmware.
- Enable learning, perform the loop a few times, read the pattern blob out, store
  it.
- Enable recognition. From then on, doing that loop yields a match plus a rep
  count.

**So you can teach this watch gestures by physically doing them, today, with
register writes. You just cannot teach it gestures the watch never physically
experienced.**

That is a real and useful distinction, and it is the opposite of what the black-box
framing assumed.

## What this is genuinely good for on a watch

The constraint — cyclic, 0.56–3.3 s, small output — actually points somewhere
specific. The data volume is trivially small:

```
1.8 Hz motion, 4-byte events  =  7.2 B/s  =  0.07 % of the 100 kHz bus
```

So bandwidth is a non-issue for this feature. It is the one thing on this chip
that fits the bus comfortably *and* does something the ESP32 cannot do without
burning cycles.

Ideas, roughly in order of how much I like them:

1. **A secret gesture as authentication.** Twist-and-return in a rhythm only you
   know. It is a *cyclic* knock pattern, which is exactly what the hardware
   recognises. Not secure in any serious sense — but as a "wake and unlock"
   gesture on a watch you wear, it is better than nothing and it is unusual.
2. **Rep counting for exercise.** Bosch's own design intent. Curls, squats,
   raises. A watch that counts your sets without you pressing anything.
3. **Shake-to-undo / shake-to-dismiss.** Cyclic by nature, in the speed window,
   and a well-understood interaction idiom.
4. **Rhythmic activity logging** — stirring, brushing, fidgeting. Amusing, low
   value.
5. **Repetition counting for accessibility** — counting repeated motions for
   someone doing physiotherapy, without them operating anything.

What does not fit, and should be ruled out early: swipes, letters, one-way
strokes, tapping patterns, anything fast, anything slow and continuous.

## The corrected one-liner

Not *"feed it a motion and it hands back a clean version"*.

**"Teach it a loop by physically doing the loop; it then tells you when you are
doing that loop again and how many times."**

That is narrower than the original idea, and it is a real, working,
no-SDK-required capability.
