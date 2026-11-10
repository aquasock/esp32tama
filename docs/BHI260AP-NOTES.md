# The BHI260AP — what the "AI" on this watch actually is

Research note, 2026-11-09. **Nothing here was measured on the watch.** The board
is in use by the other agent's work and I was asked not to touch it over USB, so
everything below comes from two sources: this project's own measured device
inventory, and Bosch's datasheet (`BST-BHI260AP-DS000`, rev 1.1, fetched and read
in full).

## What the part is

**Bosch BHI260AP**, at **I2C `0x28`**. That address is not a guess — it is in
this project's device-tree patch, found by an ESP-IDF probe on the correct pins:

```
0x1A CST9217 touch     0x20 XL9555 expander
0x28 BHI260AP sensor   0x34 AXP2101 PMU
0x51 PCF85063A RTC     0x5A DRV2605 haptic
```

All six sit on one **bit-banged I2C bus at 100 kHz** (SDA pin 3, SCL pin 2),
powered from **ALDO4**, the "sensor" rail.

Your guess was right in spirit: it is the IMU, and step counting is one of the
things it does. But it is a good deal more than an IMU.

## The "AI" is an MCU, not a neural accelerator

This matters before anything else, because it changes what "repurpose" can mean.
There is **no NPU in this chip.** The datasheet never mentions one; there is no
neural accelerator, no tensor core, no fixed-function classifier.

What is actually inside:

| | |
|---|---|
| **CPU** | ARC **EM4** core, 16/32-bit ARCv2 ISA |
| Clock | 20 MHz (Long Run) or **50 MHz (Turbo)** |
| Performance | up to **3.6 CoreMark/MHz**, 1.6 DMIPS/MHz |
| **FPU** | yes — hardware floating point |
| MPU | yes |
| SRAM | **256 KB**, in 9 banks (7 × 32 KB configurable) |
| ROM | 144 KB, preloaded with Bosch software |
| External flash | QSPI, for execute-in-place |
| DMA | 4-channel micro-DMA |
| Cache | 2-way associative |
| Power | 950 µA @ 20 MHz running CoreMark; 2.8 mA @ 50 MHz |
| Package | LGA-44, 3.6 × 4.1 × 0.83 mm |
| Host I/F | SPI (to 50 MHz) or I2C (to 3.4 MHz) |
| Master I/F | 3, selectable from 2× SPI master + 2× I2C master |

So it is a small, floating-point microcontroller with an IMU bolted to it, meant
to run always-on algorithms without waking the host. The "AI" is software —
Bosch's *Self-Learning AI*, BSX sensor fusion, swim analytics, pedestrian
dead-reckoning — running on that ARC core.

**A 50 MHz ARC EM4 with an FPU and 256 KB of RAM is a real compute resource.**
Not a neural net accelerator, but entirely capable of running your own DSP,
classifier or control loop while the ESP32 sleeps.

## The IMU underneath

The BHI260AP integrates a **BMI270-class 6-axis IMU**: 16-bit 3-axis
accelerometer and 16-bit 3-axis gyroscope. No magnetometer, no pressure sensor —
6-DoF only, despite the datasheet mentioning 9-axis fusion (that needs an
external magnetometer on one of the master interfaces).

## The blocker, stated plainly

**Secure boot with ECDSA, and the public key is burned into OTP at the factory.**

> The BHI260AP uses the Elliptic Curve Digital Signature Algorithm (ECDSA) for
> determining the authenticity of a firmware. After compilation, the firmware is
> signed by usage of a secret private key. The matching public key is stored in
> the one-time programmable memory of BHI260AP during production.
> — datasheet §5.2.3

This applies to **both** boot modes:

- **Host boot** (`HOSTBOOT` high): the host uploads firmware over I2C/SPI into
  RAM via command `0x0002`, then polls `Boot Status (0x25)` for
  `Firmware Verify Done` or `Firmware Verify Error`.
- **Standalone boot** (`HOSTBOOT` low): the bootloader reads firmware from QSPI
  flash and verifies it the same way.

You cannot sign firmware without Bosch's key, so **arbitrary code does not run.**
That is the wall, and it is a real one.

### But it is not a closed door

The firmware image has **two sections**, and only one is locked:

> The firmware image consists of two sections, a kernel payload section and a
> user payload section. The content of the kernel payload section comprises all
> algorithms, libraries and configuration items that cannot be modified. With the
> Software Development Kit (SDK), the kernel payload is provided as a signed
> binary and is authenticated to run on the BHI260AP.
>
> The user payload section allows for any kind of custom configuration and
> extension of the existing functionality. By default, the user payload section
> of the firmware is signed with a default key provided within the SDK.

So the design intent is exactly what you want: **Bosch's kernel stays, your code
goes in the user payload.** The SDK ships a default key for signing it. A
customer-specific key is available if you ask Bosch.

## What that means in practice

**You can write your own algorithms for it**, in C, compiled for ARC EM4, linked
against Bosch's pre-signed kernel payload, signed with the SDK's default key, and
uploaded over I2C at boot. What you cannot do is replace the kernel or boot
unsigned code.

The work is real but bounded:

1. **Get the Bosch SDK.** This is the gate that decides everything. It contains
   the signed kernel payload, the host-interface library, and the signing tool.
   Bosch distributes it on request — typically through their sales/FAE channel,
   and historically under a click-through or NDA. *I have not established
   whether it is publicly downloadable, and I would not assume it is.*
2. **Build an ARC toolchain.** This part is genuinely open source:
   `foss-for-synopsys-dwc-arc-processors/arc-gnu-toolchain`, and GCC has
   `-mcpu=em4`. Nothing is installed here, so it would be a build.
3. **Write the user payload** — plain C against the SDK, using the virtual-sensor
   and FIFO interfaces to get data in and results out.
4. **Host side:** a Linux driver. None exists. Kernel 7.2.4 has
   `drivers/iio/imu/bmi270/` but that speaks the BMI270's raw register interface,
   **not** the BHI260AP's command/FIFO protocol — `bmi260` in that driver is a
   different part in the same family, not the BHI260. The BHI260AP would need its
   own driver, or a userspace implementation of the protocol (which is fully
   documented in datasheet §12–13: register map, commands, parameter interface,
   FIFOs).

## The bandwidth question, before you get excited

Everything goes over the **shared 100 kHz bit-banged I2C bus**, alongside the
PMU, touch controller, RTC, expander and haptic driver.

At 100 kHz, one byte is 9 bit-times = **90 µs**. A 6-axis sample is 12 bytes of
payload plus addressing and FIFO framing — 14 bytes, **1.26 ms per sample**:

```
 50 Hz  ->   6.3 % of the bus   comfortable
100 Hz  ->  12.6 % of the bus   fine
200 Hz  ->  25.2 % of the bus   noticeable, and it competes with touch
500 Hz  ->  63.0 % of the bus   not viable alongside touch input
```

**The BHI260AP's whole reason to exist is to keep high-rate work off the host
bus.** If your repurposed algorithm can run *on* the chip and report only
results — "step", "gesture", "anomaly", a threshold crossing — the bus cost is
negligible and the design is sound. If you need raw high-rate samples in Linux,
the bus becomes the bottleneck long before the ARC core does.

That is the single most useful design constraint here, and it argues for
on-chip work rather than host-side work.

## What is unknown, and would need the board

All of these need hardware access, which I do not have:

- **Does the watch have QSPI flash attached to the BHI260AP?** The datasheet
  supports it for standalone boot and XiP. This project's inventory lists no such
  flash. If there is none, the ESP32 must upload firmware on every boot via host
  boot — an architectural constraint worth knowing.
- **What firmware is currently on it?** Whether it is Bosch's stock image, and
  whether the user payload is the default-signed one.
- **How the `HOSTBOOT` pad is strapped** on this board.
- **Whether the ESP32-S3's firmware does anything with it at all** today. It is
  not in the Linux device tree as a driver, and the probe found it answering, so
  it is powered and alive.

The clean way to answer all four is a bus read of the identification registers —
`Fuser2 Product Identifier (0x1C)`, `Revision (0x1D)`, `ROM Version (0x1E-0x1F)`
— plus `Feature Status (0x24)` and `Boot Status (0x25)`. That is a handful of
I2C reads and needs no firmware of any kind. **Worth doing when the board is
free**, and it would settle most of the above in one pass.

## Verdict

Your instinct was right that there is a programmable thing in there, and it is
more capable than "step counter" suggests — a 50 MHz FPU-equipped ARC core with
256 KB of RAM is a genuinely useful co-processor.

But two things are worth knowing before planning around it:

1. **It is not a neural accelerator.** If the appeal was "AI hardware", the
   appeal is misplaced. It is a microcontroller running classical algorithms.
2. **Custom firmware needs Bosch's SDK**, because signing is not optional. That
   is an external dependency with an unknown lead time, and it gates everything
   downstream.

If the goal is a genuinely useful repurpose — something that runs continuously
and only reports results — this chip is a good fit and the bus is not a problem.
If the goal is arbitrary code on a spare core, the secure boot stands in the way,
and the right question first is not technical: **it is whether Bosch will give
you the SDK.**
