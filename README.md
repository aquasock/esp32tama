# esp32tama

A bit-exact emulator for the **Sunplus SPLB32**, the microcontroller in the
Tamagotchi Connection V3 (2006), written in portable C11.

Runs the real firmware. Boots, hatches, needs feeding, connects over infrared.

```
core/         SPLB32 CPU, EEPROM, IR link, audio  — no dependencies
frontends/    SDL2 desktop window, Linux framebuffer, benchmark driver
tools/        build script, differential test tools, segment map generator
docs/         validation record, bug list, audit, candidate survey
```

## Why

The existing SPLB32 emulator is [BrickEmuPy], which is Python. That is fine on a
desktop and hopeless on a microcontroller. This is a C port of the same
semantics, small enough and fast enough to run on an ESP32-S3 watch — measured
at **2.65x realtime** on a T-Watch Ultra (Xtensa LX7 @ 240 MHz, NOMMU Linux),
and 4.05x headless.

[BrickEmuPy]: https://github.com/azya52/BrickEmuPy

## Status

| Area | State |
|---|---|
| CPU, 151 opcodes | bit-exact vs BrickEmuPy at 10,000,000 instructions |
| ROM banking, SFR bank, RAM, display RAM | bit-exact |
| Timers, interrupts, sleep | bit-exact |
| Ports, buttons | working |
| I2C EEPROM (HT24LC08) | bit-exact |
| Infrared link | verified: 78,677 identical pin transitions |
| Audio, two channels | verified: exact event counts |
| LCD, 970 segments | IoU 0.93 vs an independent SVG render |

"Bit-exact" means a differential test against BrickEmuPy agrees on PC, all
registers, flags, cycle count and memory hashes. See `docs/VALIDATION.md`.

`docs/BUGS.md` lists twelve bugs found by that testing — including three that are
bugs **in BrickEmuPy** and are reproduced deliberately, because the ROM was timed
against them.

## Building

```sh
cd tools && ./build.sh
```

Produces `corebench`, `tamafb` and `tamagui` beside the script. Needs a C11
compiler; SDL2 only for the desktop window.

Cross-compiling for the watch:

```sh
CC=xtensa-esp32s3-linux-uclibcfdpic-gcc ./build.sh
```

## Running

**This repository contains no ROM.** The firmware is Bandai's copyrighted work
and is not distributed here. You need a Tamagotchi Connection V3 dump —
524288 bytes, sha256
`7f4da8dc89d3843b42b3ba73b9311989332706978af5367b5ef041c365a1902f` —
placed at `tools/rom.bin`.

Dumps circulate on archive.org; you are responsible for how you obtain one.

### Desktop

```sh
cd tools
./tamagui
```

Key bindings:

| key | action |
|---|---|
| `Z` / `1` / `Left` | left button |
| `X` / `2` / `Down` | middle button |
| `C` / `3` / `Right` | right button |
| `F5` / `F9` | save / load machine state |
| `M` | mute |
| `R` | reset |
| `Esc` / `Q` | quit |

State is written to `~/.tamagui.state` on exit and reloaded on start, so a pet
survives closing the window. `--new` starts fresh.

### Framebuffer (embedded Linux)

```sh
./tamafb 30          # 30 seconds, 4 fps
```

Writes to `/dev/fb0`. Renders in 40-row bands so the working set stays small.

### Benchmark

```sh
./corebench rom.bin
```

## Display

The LCD is a 32x30 dot matrix plus 10 status icons — 970 segments. Dot size is a
build-time preference, set when the segment map is generated:

```sh
cd tools
python3 gen_segmap.py <svg> ../frontends/segmap.c ../frontends/segmap.h
SEG_DOT=4 python3 gen_segmap.py <svg> ../frontends/segmap.c ../frontends/segmap.h
```

| dot | gap | output | fits 320x400 |
|---|---|---|---|
| 4 | 1 | 243x250 | yes |
| **5** | **1** | **292x300** | **yes — default** |
| 6 | 1 | 340x350 | desktop only |

Dots are snapped to whole pixels rather than scaled, so the 1-pixel gap stays
crisp at any size.

## Testing

```sh
cd tools
gcc -O2 -I../core -o /tmp/fuzzcore fuzzcore.c ../core/*.c
python3 optest.py                       # every opcode, one at a time
python3 fuzz_diff.py 400 --seed 11      # random programs, differential
```

`ref_trace.py` drives BrickEmuPy to produce reference traces; set `BRICKEMUPY` to
its checkout.

The test tools were themselves tested: injecting a deliberate cycle-count bug
must make them fail. An earlier version of the opcode test passed 151 opcodes
while executing nothing at all, because the synthetic ROM was mapped to the
wrong flat offset. `docs/BUGS.md` records that.

## Licence

GPL-3.0. See `LICENSE`.

This is a derivative work of [BrickEmuPy] (CC0), whose semantics it reproduces
including its bugs. The segment map is derived from BrickEmuPy's CC0 SVG
artwork. See `ATTRIBUTIONS.md`.
