# Tamagotchi P1 / P2 (E0C6S46) — scope

Decision record, written 2026-11-09, before any work. Supersedes the estimates in
`E0C6200-SCOPE.md` where they disagree; the corrections are called out.

## The short version

**It would work, it would be easy, and it would be almost entirely un-novel.**

TamaLib exists, is mature, is licence-compatible, and has already been ported to
**ESP32**, Arduino UNO, Flipper Zero, Watchy and Android. This is the most
emulated Tamagotchi generation there is. That is the same signal that closed the
Tama-Go, and it should be weighed before any code is written.

It is not the *same* situation though: the Tama-Go was genuinely blocked by a
figure cartridge that is not in the dump. Here nothing is blocked. The only
question is whether a fourth ESP32 wrapper is worth the effort.

## Hardware, corrected

| | Value | Source |
|---|---|---|
| CPU | Epson **E0C6S46** | TamaLib `cpu.h` |
| Tick | **32,768 Hz** | `cpu.c: #define TICK_FREQUENCY 32768` |
| Secondary clock | 1 MHz (OSC3) | `cpu.c: OSC3_FREQUENCY` |
| RAM | 4096 × 4 bits | `cpu.h: MEMORY_SIZE 4096` |
| ROM | 12 KB max | per earlier measurement |
| Display | 32 × 16 B/W, plus 8 icons | TamaLib README |
| Variants | P1, P2 = E0C6S46; Angel, Umino/Ocean, Morino, Mothra, Tamaotch, Genjintch = E0C6S48 | TamaLib README |

> **Correction to `E0C6200-SCOPE.md`.** That document says "1.6 MHz for the P1"
> and extrapolates "~6-8x realtime on the watch" from it. The actual tick is
> **32,768 Hz** — 49× slower than that figure, and 122× slower than the SPLB32
> we already run at 2.65× realtime. Performance is not a question here at all;
> it is three orders of magnitude away from being a question.

## Prior art

**TamaLib** — <https://github.com/jcrona/tamalib>

- Copyright (C) 2021 Jean-Christophe Rona
- **GPL-2.0-or-later** — explicitly "or (at your option) any later version", so
  it can be taken under our GPL-3.0. Licence-compatible.
- 2,730 lines total: `cpu.c` 2,016, `hw.c` 150, `tamalib.c` 137, headers 427.
- Deliberately hardware-agnostic. A port implements one `hal_t` struct of about
  a dozen function pointers: alloc/free, halt, logging, `sleep_until` /
  `get_timestamp`, `update_screen`, `set_lcd_matrix`, `set_lcd_icon`,
  `set_frequency`, `play_frequency`, and an event handler.
- **Ships no ROMs**, which matches our policy exactly.

**Existing ports that matter:**

| Port | Target | Note |
|---|---|---|
| `RBEGamer/TamagotchiESP32` | **ESP32** | ArduinoGotchi — the direct precedent |
| `GaryZ88/ArduinoGotchi` | Arduino UNO (ATmega328) | 16 MHz AVR, so 32 kHz is easy anywhere |
| `lawlietrdo/tamagotchi-p1-flipper` | Flipper Zero | Cortex-M4 |
| `Gavadis3646/tamawatchy` | Watchy (ESP32) | also an ESP32 |
| `twelvehouse/TamaDroid` | Android | |

Two of those are **ESP32**, one of them on a watch. That is the honest headline:
someone has already shipped this on this chip family, twice.

## What it would actually cost

Small. Smaller than Pokémon Mini by a wide margin.

| | Pokémon Mini (done) | E0C6S46 |
|---|---|---|
| Core to bring over | ~6,200 lines, 5 CPU table files | 2,730 lines, 1 `cpu.c` |
| Porting surface | full platform layer | ~12 HAL callbacks |
| Clock | 4 MHz | 32.768 kHz |
| Frame budget pressure | the real risk | none |
| Instruction work | 98 handlers + 4,096-entry table | already implemented upstream |

The realistic path is **vendor TamaLib**, exactly as PokeMini was vendored: keep
it verbatim in `core/tamalib/`, write a `hal_types.h` and a HAL implementation,
and reuse the existing front end. That is days, not weeks.

Our own half-written `core/e0c6200.c` (machine layer only, no `step()`) becomes
redundant under that plan. That is fine — it was an experiment, and 163 lines of
sunk cost is not an argument.

## The two real questions

**1. Is novelty worth anything here?**

The V3 is novel: SPLB32 has two `.brick` devices and essentially nobody has
ported it. That is why it is the flagship. P1/P2 is the opposite — it is the
first thing anyone emulates, with a reference library that has been ported to
eight platforms including two ESP32s.

If the appeal of this project is "we did something first", P1/P2 adds nothing.
If the appeal is "our watch runs the things people want", P1/P2 is the single
best-supported addition available, and it would take days.

**2. Would it dilute the V3?**

The V3 was deliberately kept undiluted: no Digimon, no older generations. Adding
P1/P2 to the same front end is exactly the dilution that was ruled out earlier.
The counter-argument is that P1/P2 is a *different platform*, not another
Tamagotchi variant of the same one — the V3 is connection-era hardware, P1/P2 is
the original 1996 chip, and they share nothing but the brand.

Worth deciding explicitly rather than by drift.

## If it goes ahead

1. **Vendor TamaLib** into `core/tamalib/`, verbatim, with `hal_types.h`
   generated from the template. Attribution in `ATTRIBUTIONS.md`, GPL-2.0-or-
   later taken under GPL-3.0.
2. **Write the HAL** — ~12 callbacks against SDL2, reusing `pmgui.c`'s window,
   pacing and key handling. The 32×16 panel is far simpler than 96×64.
3. **Verify against TamaTool**, which is the same author's explorer and can be
   run side by side. Same differential discipline as the V3 and Pokémon Mini.
4. **ROM policy** — no ROM ships. P1 and P2 dumps exist (both were extracted
   from die photographs) and the operator supplies them, as with the other
   platforms.

Estimated effort: **2–4 days** for a desktop build, most of it the HAL and the
segment/icon rendering, which is 512 pixels plus 8 icons rather than a full
framebuffer.

## Recommendation

Scope it, then decide deliberately rather than by momentum.

- If the goal is **coverage**, do it — it is cheap and it is the most-wanted
  platform available.
- If the goal is **distinctiveness**, skip it and put the effort into the watch
  port of Pokémon Mini, which is genuinely unported *and* already has a working
  reference here.

What I would not do is start it because it looks easy. It is easy, and that is
precisely why it is already done twice on this chip family.
