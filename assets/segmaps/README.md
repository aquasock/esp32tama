# Segment maps — measured, not assumed

`assets/segmaps/*.json` are the LCD segment maps for each first-gen device,
extracted from the SVG face artwork that ships inside the MAME ROM zips.

## Format

Each file maps a breadboard-nibble and bit to a panel coordinate:

```json
"12,3": [7, 5]
```

The key is `nibble,bit` — which display RAM nibble and which of its four bits.
The value is `[x, y]` on a 32×16 panel, derived from the x/y ordering of the
rectangles in the artwork.

Deriving the coordinate is the whole trick: the `<title>` elements inside each
SVG carry a *numbering* that does **not** correspond to the panel grid (they run
0..15 on one axis and 0..36 on the other, which is neither 32 nor 16). The
`<path d="m X,Y h W v H">` geometry does. Sorting the distinct X and Y values
gives a clean 32 × 16 grid at 11.0 units pitch, which is the panel.

## What the maps say

```
device     bits  grid    vs TamaLib
p1         512   32x16   exact 512 / 512
p2         512   32x16   exact 512 / 512
angel      512   32x16   exact 512 / 512
mothra     512   32x16   exact 512 / 512
digimon    512   32x16   differs — see below
```

**The whole Tamagotchi family shares one map.** P1, P2, Angel and Mothra produce
byte-identical tables. That is a real finding: the E0C6S48 devices (Angel,
Mothra) do not need a different segment map from the E0C6S46 ones, despite the
extra 11 segments in the S48 table being available. TamaLib's two separate
`seg_pos` arrays therefore agree on everything the 32×16 panel actually uses.

## Digimon is wired differently, and TamaLib gets it wrong

Digimon's map does not match TamaLib's table in **448 of 512** entries — it is
close to a mirror in segment order, not a variation:

```
TamaLib (s48):  0,1,2,3,4,5,6,7,32,8,9,10,11,12,13,14,15,... ,31,30,...,24,36,23,...
Digimon:       32,31,30,29,28,27,26,25,24,...,23,22,21,20,19,18,17,16,0,1,2,3,4,5,6,7,...
```

Rendering a Digimon ROM with TamaLib's built-in map gives a plausible but wrong
creature: 47 lit pixels and a ragged outline. With the measured map it is 65
pixels and a coherent shape.

The measured map was cross-checked against an **independent source**: the
`lcd_layout.json` from `willmcginnis/digimon-vpet-1997-disassembly`, itself
derived from the ROM's own disassembly rather than from artwork. Result:

```
Digimon SVG map vs Digimon disassembly layout:  exact 512 / 512
```

Two completely independent derivations agree exactly. **TamaLib's table is the
outlier.**

## Using these

`core/tamalib/hw.c` hardcodes `seg_pos` at compile time. To use a measured map,
replace that array. The values are `segment -> x`, with any value `>= 32`
meaning "this is an icon segment, not a pixel"; the icon index is chosen
separately by `seg == 8` / `seg == 28`, so collapsing icon slots to 32 is safe.

```
p1/p2/angel/mothra (identical):
  0,1,2,3,4,5,6,7,32,8,9,10,11,12,13,14,15,32,32,32,31,30,29,28,27,26,25,24,32,
  23,22,21,20,19,18,17,16

digimon:
  32,31,30,29,28,27,26,25,24,32,32,32,23,22,21,20,19,18,17,16,0,1,2,3,4,5,6,7,32,
  8,9,10,11,12,13,14,15
```

## Licensing

The JSON files here contain **coordinate pairs only** — which segment lights
which dot. That is a factual measurement of the hardware, in the same category
as a pinout, and it is what this repository stores.

The SVG artwork itself is **not** committed. It ships inside the MAME ROM zips,
declares no licence in its metadata, and is not ours to redistribute. The maps
were derived from it and the derivation is reproducible from a ROM set the
operator already holds.

## Reproducing

```python
import re, json
s = open('tama.svg').read()
pts = []
for m in re.finditer(r'id="(\d+)_(\d+)"[^>]*?\bd="m\s*([-\d.]+),([-\d.]+)', s):
    pts.append((int(m.group(1)), int(m.group(2)), float(m.group(3)), float(m.group(4))))
ux = sorted({p[2] for p in pts}); uy = sorted({p[3] for p in pts})
xi = {v:i for i,v in enumerate(ux)}; yi = {v:i for i,v in enumerate(uy)}
segmap = {'%d,%d' % (n,b): [xi[x], yi[y]] for n,b,x,y in pts}
```

Any device whose ROM zip contains an `.svg` can be mapped this way in a few
lines, which means adding a device is measurement rather than guesswork.
