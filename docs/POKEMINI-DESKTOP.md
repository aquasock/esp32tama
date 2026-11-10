# Pokémon Mini — desktop reference build

The Linux front end, which is the reference we judge the watch port against.
Same role `tamagui` plays for the V3: get it working and playable on a normal
machine first, then carry it across.

## Status

**Playable, across the whole cartridge set we hold.** Confirmed by the operator,
in play, on 2026-11-09: Pokémon Pinball started, the clock was set, and **two
levels were cleared**. That is the only test that covers the whole chain at
once — BIOS handover, cartridge execution, PRC rendering, display refresh and
input — and it passed.

Every one of the twelve images was then played, not merely swept. Operator
verdict per image, all in play on 2026-11-09:

```
Pokemon Pinball Mini (USA, Europe)                  pass  - two levels cleared
Pokemon Tetris (Europe) (En,Ja,Fr)                  pass
Pokemon Zany Cards (USA, Europe)                    pass
Pokemon Puzzle Collection (USA, Europe)             pass
Pokemon Party Mini (USA)                            pass
Lunch Time (USA) (GameCube)                         pass
Snorlax's Lunch Time (Europe) (GameCube)            pass
Party Mini - Baseline Judge (USA) (GameCube)        pass
Party Mini - Chansey's Dribble (Europe) (GameCube)  pass
Party Mini - Ricochet Dribble (USA) (GameCube)      pass
Party Mini - Rocket Start (USA) (GameCube)          pass
Party Mini - Slowking's Judge (Europe) (GameCube)   pass
```

Ricochet Dribble was additionally cold-relaunched mid-round and replayed from a
fresh PID, which rules out a warm-process artefact. No save state exists on
disk, so every launch was a genuine cold boot.

**Audio has its first listening test.** Tetris's title music played correctly in
the same round. That was the one path the front end carried with no listening
test behind it, and it now has one — on one cartridge, which is a first
datapoint rather than a closed question.

This is coverage of five distinct retail products (Pinball, Tetris, Zany Cards,
Puzzle Collection, Party Mini) plus the five Party Mini variants and the two
Lunch Time discs. It is **not** the full commercial library: Race mini, Pichu
Bros. mini, Breeder mini and Sodateyasan mini are not in this set and remain
untested.

Before the play round, verified on a desktop with the actual BIOS: all twelve
boot and render, with no cartridge blanking or hanging after power-on.

What each automated check covers, so the gaps are obvious:

| Check | What it proves |
|---|---|
| `tools/pokemini_keys.c` | every key maps to the right keypad bit |
| `tools/pokemini_bench.c` | instructions per frame against the 13,889 budget |
| 12-cartridge frame sweep | no cartridge blanks or hangs after power-on |
| determinism replay | which button actually changes the screen |

None of those prove playability. Playing it did.

## Build and run

```
./tools/build_pmgui.sh
./pmgui <bios> <cartridge> [--scale N]
```

Needs SDL2 and zlib development files.

- `<bios>` is a real BIOS dump. The free BIOS from PokeMini's tree runs homebrew
  but is not a substitute for retail cartridges; see *BIOS* below.
- `<cartridge>` may be a bare `.min` **or a `.zip` containing one**, which is how
  the MiSTer/MAME sets ship. No extraction step is needed.
- `--scale` defaults to 6, giving a 576×384 window; F1/F2 change it at runtime.

## Keys

```
arrows        d-pad
Z or Enter    A
X             B
C             C
P             Power    <- deliberately not Enter
F5 / F9       save / load state
F1 / F2       window smaller / larger
M             mute
Esc           quit
```

Power used to be Enter. On a real console Power suspends the machine, so the
key most people reach for first would blank the screen and look like a crash.
Enter is now a second A, and Power moved to P where it has to be chosen.

## The key mapping was wrong, and that was the white screen

`PokeMini_KeypadEvent()` takes a **`MINX_KEY_*` enum value (1..9)**, not a bit
mask. The first version passed bit masks, and the result was silent chaos:

| Key pressed | value sent | key the core saw |
|---|---|---|
| Z | 0x02 | **B** |
| X | 0x01 | **A** |
| C | 0x40 | *nothing — no such key* |
| Up | 0x04 | Up (correct, by luck) |
| Down | 0x08 | **POWER — suspends the console** |
| Left | 0x10 | *nothing* |
| Right | 0x20 | *nothing* |
| P | 0x80 | *nothing* |

So pressing **Down in a game powered the console off**, which is the reported
white screen. C, Left and Right did nothing at all, which is why the Diglett
would not respond to C. A and B were swapped.

The mapping now uses the enum names, so this class of mistake is a compile error
rather than something a player discovers. `tools/pokemini_keys.c` pins it:

```
PASS up     -> bit 0x08 (want 0x08)
PASS down   -> bit 0x10 (want 0x10)
PASS left   -> bit 0x20 (want 0x20)
PASS right  -> bit 0x40 (want 0x40)
PASS A      -> bit 0x01 (want 0x01)
PASS B      -> bit 0x02 (want 0x02)
PASS C      -> bit 0x04 (want 0x04)
PASS power  -> bit 0x80 (want 0x80)
PASS down does not press Power
OK
```

Which button does what, measured by deterministic replay in Pokémon Pinball
(hold a key for 20 frames at a fixed point, compare against an identical run
with no key):

```
A      differs 70 px   <- this is the one that responds
DOWN   differs 38 px
UP     differs 17 px
B      differs  0 px
C      differs  0 px
LEFT   differs  0 px
RIGHT  differs  0 px
```

So for the Diglett flippers, **A** is the live button, and it is now on **Z or
Enter**. B is mapped correctly too and will respond where the game uses it.

## Other bugs found in the same round

The operator reported a white screen. It was **not reproducible** in the core:
all twelve cartridges were driven for 1,500 frames each and every blank period
was confined to power-on (frames 10-50) and a second one during the title
transition (190-230). No ROM ever hung blank, and none ended with an empty
screen. Twelve seconds of scripted input on the running window produced no
blanking either.

Three real bugs were found while looking, all fixed:

1. **Audio was half-silent garbage.** `MinxAudio_GetSamplesS16` takes a count of
   int16 *values*, and the reference front end passes `len >> 1`. This front end
   passed `want_bytes / 4`, so half of every buffer was uninitialised memory
   queued as samples.
2. **The emulator ran 7 % fast.** Pacing used `1000 / 72`, which truncates to
   13 ms — that is 76.9 Hz, and the audio buffer drained against it. Pacing is
   now fractional microseconds.
3. **Power was on Enter**, as above.

And one visibility fix, which is the likely explanation for the report: an
all-unlit screen used to render as a full-window near-white rectangle with no
border, indistinguishable from a dead window. The panel now sits in a dark bezel
on a soft off-white, so an empty screen is visibly an empty *screen*, and a
watchdog prints `display: 90 frames with nothing lit (still running)` if the
panel stays empty for three seconds.

Save states go to `<cartridge>.state` next to the cartridge, using PokeMini's
own snapshot format. Verified: PC `003313` → `032326` after 400 more frames →
restored to `003313` on load.

## Where ZIP support lives, and why there

In the **front end**, not the core. `frontends/pmgui.c` carries a small ZIP
reader using zlib's raw-deflate path. The core links against failing stubs
instead (`core/pokemini/minizip_stubs.c`), so the embedded build never carries
zlib — which matters when flash is the scarcest thing on the board.

The trade is deliberate: convenience on the desktop, no cost on the target.

## BIOS

Retail cartridges need a retail BIOS. The free BIOS that ships with PokeMini
(`core/pokemini/freebios.c`, 4 KB, GPL) idles at `$0040` and does not hand over
to a commercial cartridge, so it is useful for checking reset and the memory map
and not much else.

A real dump is 4096 bytes and starts `9A 00 AB 00 AB 00 CF 01`; the free BIOS
diverges at offset 6 (`... 2F 02`). Nothing from a BIOS dump is in this
repository and none should be.

## The bug that made the first harness lie

Worth repeating here because the front end had the same shape:
`PokeMini_SetMINMem()` only points the core at the image. Without
`PokeMini_Reset(0)` afterwards the CPU stays wherever `PokeMini_Create()` left
it, the cartridge never starts, and the core still executes happily — which
reads as a working emulator producing a suspiciously stable instruction count.
Both `pokemini_bench` and `pmgui` now reset after loading.

## What this does not yet do

- **Audio has one listening test, not a suite.** The path is wired (44.1 kHz,
  `MinxAudio_GetSamplesS16` into a queued SDL device), the mute key works, and
  Pokémon Tetris's title music played correctly in the 2026-11-09 play round.
  That retires the old "never listened to" gap for one cartridge. It does not
  establish that the rest of the sound path is right — a cartridge that leans on
  a channel, a waveform or a volume behaviour Tetris does not use could still be
  wrong with nothing here to catch it. Treat audio as *started*, not *settled*.
- **No dirty-rectangle rendering.** It redraws all 96×64 every frame, which is
  free on a desktop. The watch port cannot afford that; see
  `POKEMINI-DISPLAY-GATE.md`.
- **No frame skip.** Pacing is a 72 Hz deadline with resynchronisation if more
  than one frame is missed, so a slow host runs behind rather than speeding the
  machine up.
- **Shades are flat.** The core computes a decayed intensity array
  (`LCDPixelsA`) for the original's fading dots; the front end draws
  `LCDPixelsD` as crisp black on white. Good for a reference, not faithful to
  the panel's grey.

  Measured, so nobody later mistakes it for a bug: `LCDPixelsD` is strictly
  binary — the histogram over a Pinball title screen is `0: 1935, 1: 4209` and
  nothing else. `LCDPixelsA` carries 4 distinct intensities. So the heavy
  checkerboard in the title art is the game's own spatial dithering, not a
  decoding fault, and drawing it one bit per dot is correct.
