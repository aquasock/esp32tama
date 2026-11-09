# Run this on the watch

One binary, fully static Xtensa, needs nothing from the watch's libc.

```
pokemini_bench_target   304 KB on disk
                        text 184 KB   data 4 KB   bss 56 KB   no heap at all
```

## What it is

A headless benchmark for the Pokémon Mini emulator core this project has been
building. It measures **how many emulated CPU instructions per second the watch
can retire**, which is the last unknown between us and shipping that platform.

The Pokémon Mini runs at 4 MHz with a 4-cycle instruction fetch, so **realtime
needs 1,000,000 instructions/second**. At 72 fps that is a budget of 13,889
instructions per frame. Above 1M/s is headroom; below it the platform cannot run
at full speed.

## Run it

Copy the binary to the watch's writable `/home` partition, then:

```sh
cd /home
chmod +x pokemini_bench_target
./pokemini_bench_target --synthetic 2000
```

It takes a couple of seconds and needs **no ROM file**. `--synthetic` generates
a pseudo-random 32 KB cartridge in memory, which exercises the same dispatch,
memory and interrupt paths a real game would — the interpreter does not care
what the opcodes mean.

Take these two lines:

```
core      68735641 instructions/sec     <- this one
realtime  289.12x  (1.00x = 72 fps)
```

(Those figures are from a desktop, not the watch. We are asking what the watch
gives.)

If you have a `.min` cartridge image available you can also run a real one:

```sh
./pokemini_bench_target /home/some-game.min 2000
```

A real ROM usually comes out **faster** than the synthetic one, because random
opcodes hit the slow paths. So synthetic is the pessimistic number, which is the
one that matters for a go/no-go.

## What to send back

The whole output is fine, but the lines that matter are `core` and `realtime`.
If convenient, run it three times and send all three — this board's clock
scaling makes single runs a little noisy.

## If something goes wrong

- **`Illegal instruction` or a signal** — send the exact message. The binary is
  static and built for `xtensa-esp32s3-linux-uclibcfdpic`, the same triple your
  own userspace uses, so a failure here would mean something specific and worth
  knowing.
- **Hangs** — it does no I/O, opens no socket and has no blocking path, so this
  would be a genuine finding. The whole run is a few seconds at most; per your
  notes on this board a wedged process may need a chip reset, so start it in the
  foreground and it will finish before that becomes relevant.
- **Out of memory** — should not happen: there is no `malloc` in the synthetic
  path at all. Writable footprint is 4 KB data plus 56 KB bss, the latter mostly
  the static cartridge buffer. That is deliberate, because this board is NOMMU
  and has been bitten by fragmented allocation before.

## Where the binary is

The compiled binary is **not** in git — it is a build artefact. It is sitting on
disk here:

```
/run/media/vash/GIT/tinytama/for-escapement/pokemini_bench_target
```

Rebuild it in one command if it is missing or stale.

## How it was built

```sh
./tools/build_pokemini_target.sh
```

Uses `Escapement/build/toolchain/crosstool-NG/builds/xtensa-esp32s3-linux-uclibcfdpic`
with `XTENSA_GNU_CONFIG` set to the S3 dynconfig — without that the compiler
emits big-endian objects and the link fails against the little-endian uClibc
sysroot with a misleading "file in wrong format". Your `build-userspace.sh`
already documents that trap; this script follows the same recipe.

Source: `tinytama/tools/pokemini_bench.c` plus the vendored PokeMini core
(GPL-3.0, `tinytama/core/pokemini/`).

No ROM is included and none is needed.

## What happens with the answer

- **Above ~1.5M/s** — headroom, and the port proceeds at 3× zoom. Your display
  measurement already says 51 fps is available there, so this closes the last
  gate.
- **Around 1M/s** — realtime with no margin. Still shippable for a game that is
  not twitch-driven, and worth knowing precisely.
- **Below 1M/s** — the platform does not ship, and we say so rather than porting
  something that runs slow.

Either way it is a number we do not currently have, and guessing it would be the
third time on this project that a plausible estimate turned out wrong.
