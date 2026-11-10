# Catalogue: what each scoped CPU actually gives you

Every list below comes from BrickEmuPy's own device configs, cross-checked
against ROM sizes where a ROM is present. Where I add product knowledge that is
not in the repository, it is marked `[general knowledge]`.

## A correction on the counts

The device counts in the earlier survey overstate variety, because regional
variants are separate entries:

```
E0C6200   18 entries   but 8 distinct Tamagotchi products and 4 distinct
                       Digimon products (V1 exists in JA, JB and ES)
SPLB20     6 entries   Apollo Prince & Princess is EN and CH
```

Worth knowing before picking a target by device count.

---

## E0C6200 — Epson 4-bit, ~1.6 MHz, 12 KB ROM

The originals. 18 config entries, about 15 distinct products.

**Tamagotchi line**

```
Tamagotchi P1 (1996)        the first one; 82M+ sold [general knowledge]
Tamagotchi P1J              Japanese release
Tamagotchi P2 (1997)        second generation
Tamagotchi Angel (1997)     Tenshitchi — angel-themed, "good deeds" mechanic
Tamagotchi Umino (1997/98)  "Umino Tamagotchi" = Ocean / Oceangotchi — water theme
Tamagotchi Morino (1997/98) "Mori no Tamagotchi" = Forest — insect characters
Tamagotchi Mothra (1998)    Godzilla tie-in
Tamagotchi Genjintch (1998) "Genjintchi" — caveman / primitive theme
Tamagotchi Tamaotch (1998)  limited edition named after actress Tamao Nakamura
Tamagotchi Yasashii (1998)  "gentle" — easiest of the line, 9 buttons
```

**Digimon line**

```
Digimon V1  (JA / JB / ES)  the original Digimon virtual pet, 1997
Digimon V2  (1997)
Digimon V3  (1998)
Digimon V4  (1998)
```

**Other**

```
AlienFever                  ROM present
RadioShack Stack Challenge  ROM present
nikko                       ROM present
```

**ROM availability: 3 of 18.** Only AlienFever, Stack Challenge and nikko ship a
ROM. Every Tamagotchi and every Digimon is distributed without one — confirmed
against both MAME 0.289 (63,685 zips) and BrickEmuPy's own README.

These are all **virtual pets, not games in different genres.** Each themed edition
is the same 3-button egg form factor with different firmware and artwork — which
is exactly why one emulator core covers all of them. The exceptions are Yasashii
(9 buttons: food, smile, heart, clean, bulb, weight, select, cancel, reset) and
Tamaotch (a celebrity tie-in).

Three of them — Umino, Morino, Genjintch — share byte-identical shell geometry in
BrickEmuPy's artwork (696x858 viewBox, 520 segments, 3 buttons), confirming they
are the same hardware with different ROMs.

**Why it's still the best choice:** it is the actual lineage. P1 and P2 are the
devices the word "Tamagotchi" means to most people, and Angel/Umino/Morino are
what collectors ask for.

---

## Sharp SM510 — 4-bit, 32 kHz, 4 KB program

NOT in BrickEmuPy. From MAME's `hh_sm510.cpp`, which drives **59** Game & Watch
titles. The 1989 Zelda ZL-65 dump verified on the desktop is one of them.

Top titles [general knowledge, cross-checked against MAME's ROM list]:

```
Zelda (ZL-65, 1989)          the one you have; multi-screen
Mario Bros. (1983)           the first multi-screen G&W
Donkey Kong (1982)           the one that started the craze
Donkey Kong II (1983)
Donkey Kong Jr. (1982)
Oil Panic (1982)             the Game Boy Camera Easter egg references it
Fire (1981)                  the very first Game & Watch
Parachute (1981)
Octopus (1981)
Popeye (1983)
Mickey & Donald (1982)
Snoopy (1983)
Green House (1982)
Turtle Bridge (1982)
Manhole (1981)
Vermin (1980)                the actual first, silver series
Mario's Cement Factory (1983)
Lifeboat (1983)
Tropical Fish (1981)
Egg (1981)
```

**ROM availability: none in BrickEmuPy.** You have one — the 1989 Zelda, CRC
verified against MAME.

**The cost:** a genuine CPU port, ~59 titles if you source ROMs. Biggest
recognisable library of anything on this list.

---

## Holtek HT4BIT / HT943 — 4-bit, up to 1 MHz, 4 KB

The generic LCD handhelds. 174 lines of its own plus a shared 689-line base that
also serves HTG12N0.

```
E23PlusMarkII96in1     ROM    the 96-in-1 Brick Game
E88_8in1               ROM
GA888                  ROM
Keychain55in1          ROM
KeychainPinBall        ROM
Puyolin                ROM    Puyo Puyo clone
SpaceIntruderTK150I    ROM    Taito Space Invaders handheld
MameGalaxian           no ROM
MameTamagotch          no ROM
```

**ROM availability: 7 of 9.**

The 96-in-1 is probably the most-owned LCD handheld ever made — the translucent
grey brick with Tetris, Snake, Formula 1 and about ninety variants thereof
[general knowledge]. Not a *game* in the Tamagotchi sense, but culturally the
widest reach on this list.

**Cheapest real win**: 7 playable ROMs today, and the base class is shared.

---

## Toshiba T7741 — 4-bit, ~190 kHz

Twelve entries, eleven with ROMs. Japanese licensed properties.

```
Pengo                ROM    Sega arcade penguin, 1982
Doraemon             ROM    IsogeDoraemon — the anime cat robot
Dr. Slump            ROM    Arale-chan — Toriyama, pre-Dragon Ball
Gundam Gaiden II     no ROM
Tom & Jerry          ROM
Par-Man              ROM    Pac-Man clone
Gakken Soccer        ROM
Power Fishing        ROM
Jumping Boy          ROM
Dorayaki House       ROM
Spica D'Artagnan     ROM
Space Cobra          ROM    Animest — the Leiji Matsumoto anime
```

**ROM availability: 11 of 12.**

At 190 kHz this is the **cheapest emulation on the list by a wide margin** — the
watch could run it dozens of times faster than real time with cycles to spare.

---

## Sunplus SPLB20 — 8-bit, ~1.1 MHz

Six entries, and its instruction set is a **strict subset of the SPLB32 we
already built** — all 71 handlers exist in our core. Only the memory map differs.

```
Gyaoppi 9-in-1         ROM    32K
Big Cat 9-in-1         ROM    32K
Elfin Twins            ROM    32K
Apollo Prince & Princess (EN)  ROM  32K
Apollo Prince & Princess (CH)  ROM  32K
Apollo 2-in-1          no ROM
```

**ROM availability: 5 of 6.**

These are the knock-off multi-pets — Gyaoppi is the best-known "generic
Tamagotchi" chipset [general knowledge]. As you said: bootleg energy. But the
core is nearly free, since we already have the instruction set.

---

## Pokémon Mini — Seiko S1C88, 4 MHz, 96x64

Not in BrickEmuPy; assessed from `libretro/PokeMini` source. Exactly **ten**
commercial games were released, which makes the list complete rather than a
top-ten. Verified against Wikipedia's release table, which carries North
America / Japan / Europe dates per title, and cross-checked against MAME 0.261's
`pokemini` software list (43 images, all regional and preview variants).

```
Title                              NA        JP        EU     held
Pokémon Party mini              2001-11-15 2001-12-14 2002-03-15  yes
Pokémon Pinball mini            2001-11-15 2001-12-14 2002-03-15  yes
Pokémon Puzzle Collection       2001-11-15 2001-12-14 2002-03-15  yes
Pokémon Zany Cards              2001-11-15 2001-12-14 2002-03-15  yes
Pokémon Tetris                     --      2002-03-21 2002-09     yes
Pokémon Puzzle Collection vol. 2   --      2002-04-26    --        no
Pokémon Race mini                  --      2002-07-19    --        no
Pichu Bros. mini                   --      2002-08-09    --        no
Togepi's Great Adventure           --      2002-10-18    --        no
Pokémon Breeder mini               --      2002-12-14    --        no
```

**All five unheld retail titles are Japan-only.** They never left Japan, which
is why a Western cart set covers five of ten and stops.

Three naming traps, all of which earlier versions of this file got wrong:

- **Pokémon Breeder mini is ポケモンそだてやさんミニ** — *Sodateyasan* mini. These
  are **one game, not two**, and not a tenth mystery title. MAME's `pokesoda`
  is the same product as the Breeder mini row above.
- **Pokémon Anime Card Daisakusen (ポケモンアニメカード大作戦) is the Japanese
  release of Pokémon Zany Cards**, not a separate game. MAME's `pokeacrd` is
  that JP cart; the three Western Zany Cards entries sit alongside it.
- There is **no "Pokémon Kogane"**. An earlier version of this list guessed at a
  missing tenth title by that name; the tenth row is Togepi's Great Adventure.

Separately from the ten retail carts, six Party mini minigames were distributed
two ways — inside the retail Party mini cartridge, and as individual GameCube
bonus discs. Plus Lunch Time / Snorlax's Lunch Time as GameCube discs. Those are
not part of the ten.

Plus an active **homebrew** scene with freely licensed ROMs.

**The decisive advantage:** a **free BIOS** ships with the emulator
(`freebios/freebios.min`, GPL-3.0). This is the only platform here you could
build and ship legally, with no proprietary dump needed.

---

## Cross-reference: clocks, and what they imply

```
Toshiba T7741        190 kHz    ~21x slower than the SPLB32 we measured
Sharp SM510           32 kHz    ~125x slower
Sunplus SPLB20       1.1 MHz    ~3.6x slower
Holtek HT943          1 MHz     ~4x slower
Epson E0C6200        1.6 MHz    ~2.5x slower
Pokémon Mini           4 MHz    same as SPLB32 -> 2.65x measured
```

Everything except Pokémon Mini has **more** CPU headroom than the core we
already proved on the watch. The limiting factor for all of them is the display
path, not the emulation — and all of them except the Pokémon Mini have
32x16-ish dot displays, which are far cheaper to paint than our 292x300.

## What I would pick, and why

**E0C6200** — the actual Tamagotchi and Digimon line, 15 products, and the
machine layer is already written and verified. Accept that you must source ROMs.

**Pokémon Mini** — if you want something that works the day it builds, legally,
with a free BIOS and homebrew.

**HT4BIT** — if you want the cheapest useful port: seven ROMs in hand, shared
base class, and the 96-in-1.
