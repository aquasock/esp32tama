# Attributions

Not all of this is my work. The parts that are not are listed here, with what
they contributed.

## BrickEmuPy — the emulator this is a port of

- **Author:** azya (`azya52`, [@OneBitOnePixel](https://x.com/OneBitOnePixel))
- **Source:** https://github.com/azya52/BrickEmuPy
- **Licence:** CC0 1.0 (public domain dedication)

The SPLB32 semantics in `core/splb32.c` — instruction behaviour, cycle counts,
memory map, SFR layout, sleep batching — are a C port of BrickEmuPy's
`cores/SPLB32.py`. `core/ht24lc08.c`, `core/con_v3_ir.c` and
`core/splb32_sound.c` are ports of the corresponding files in its `peripherals/`
and `cores/` directories. The LCD segment map in `frontends/segmap.c` is
rasterised from BrickEmuPy's `assets/Tamagotchi_v3.svg`.

CC0 imposes no conditions, so this repository is licensed GPL-3.0 as a whole.
The credit is given because it is owed, not because the licence requires it.

**Three bugs were found in BrickEmuPy during this port and are reproduced
deliberately**, because the firmware was timed against that implementation. They
are documented in `docs/BUGS.md`. A correct 6502 core would not run this ROM
identically.

## The ROM dump

- **Dumped by:** azya, by decapsulating the SPLB32 and reading the mask ROM from
  die photographs.
- **Published:** archive.org item `tamagotchi-v3-us-eu`

The ROM itself is Bandai's copyrighted work and is **not** included in this
repository.

## Hardware reverse engineering — background

- **Natalie Silvanovich** (https://natashenka.ca) — reverse engineering of
  Tamagotchi hardware, the GeneralPlus factory test program, and the precedent
  that these devices can be attacked through their own interfaces. Her write-ups
  shaped the investigation in `docs/findings.md`, though the emulator here is
  not derived from her code: her `Tamagotchi-Hack` repository targets the
  Tama-Go (GPLB52320A) and Friends (2013), not the SPLB32.

## Prior art worth knowing

- `jcrona/tamatool` — P1/P2 explorer and emulator
- `travisgoodspeed/maskromtool` — die-photo bit extraction
- `zacharesmer/tamagometer` — infrared protocol documentation for the 2024
  Connection revision

## This repository

Written by aquasock, 2026. The C port, the differential test harness, the
segment map generator, the front ends and the documentation are original work;
the behaviour they implement is BrickEmuPy's.

## Assets

`assets/Tamagotchi_v3.svg` is from BrickEmuPy (CC0). It is the source for
`frontends/segmap.c` and is included so the segment map can be regenerated at a
different dot size.

## Relicensing offer

This repository is GPL-3.0. BrickEmuPy, which it ports, is CC0 — a deliberate
choice by its author to place that work in the public domain.

GPL-3.0 is viral: merging this code into BrickEmuPy would force BrickEmuPy to
become GPL-3.0 as well. That is a real cost to impose on someone who chose CC0,
and it is not the intent here. The core was licensed GPL-3.0 because that is the
author's general preference, not to bind upstream.

**If azya would rather merge any of this under CC0, say so and it will be
relicensed.** Specifically, on request and without condition:

- `core/*` may be relicensed CC0 1.0
- `frontends/segmap.c` is already derived from CC0 artwork and may be CC0
- anything else requested

The offer applies to the upstream author for the purpose of merging into
BrickEmuPy. It does not require attribution beyond what is already here.

## PokeMini (Pokémon Mini core)

`core/pokemini/` is vendored from **PokeMini** by JustBurn.

- Project: <https://github.com/pokemon-mini/PokeMini>
- Revision: `8a64225e21307ee89b18feca0a429a8d2d43e6d1`
- Version: 0.61
- Licence: **GPL-3.0**, full text at `core/pokemini/LICENSE-PokeMini`
- Copyright (C) 2009-2015 JustBurn and contributors

Every file in that directory except `minizip_stubs.c`, `unzip.h` and `README.md`
is copied verbatim. One upstream file carries a two-line patch for an
instruction counter, recorded as
`core/pokemini/patches/0001-exec-counter.patch`.

The free BIOS in `core/pokemini/freebios.c` comes from the same project and
carries the same licence. It is included so the core can be exercised without
any commercial ROM.

PokeMini also ships the Pokémon Mini instruction-set reference used to sanity
check this work (`doc/` upstream), and its dev kit's homebrew examples are used
as benchmark inputs. Both are GPL-3.0.

This repository is GPL-3.0, so vendoring GPL-3.0 code into it is licence
compatible. No relicensing offer is made for `core/pokemini/` — that code is
not ours to relicense.

## TamaLib (Tamagotchi P1/P2 core)

`core/tamalib/` is vendored from **TamaLib** by Jean-Christophe Rona.

- Project: <https://github.com/jcrona/tamalib>
- Revision: `ce304d55f9a73c60232ce3f552e7983db3fa399c`
- Licence: **GPL-2.0-or-later**, full text at `core/tamalib/LICENSE`
- Copyright (C) 2021 Jean-Christophe Rona <jc@rona.fr>

Every file in that directory is copied verbatim. `core/hal_types.h` is generated
from upstream's `hal_types.h.template`; it lives in `core/` because `hal.h`
includes `"../hal_types.h"`.

TamaLib is GPL-2.0 with the "or (at your option) any later version" clause, so
it may be used here under GPL-3.0. No relicensing offer is made for
`core/tamalib/` — that code is not ours to relicense.

No ROM is distributed. The operator supplies their own P1/P2 dump.
