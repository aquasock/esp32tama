#!/usr/bin/env python3
"""
gen_segmap.py - build an accurate segment raster map from the Tamagotchi V3 SVG

Why this exists: the first version of this generator computed each segment's
bounding box from the raw numbers in the `d` attribute, assuming they alternate
x,y. They do not. The artwork uses H and V commands (1900+ of each) which carry
a single coordinate, and cubic C curves whose control points are not vertices.
The result was garbage boxes -- visible as a grid of lines across the LCD.

This parses the path properly, flattens curves, and rasterises each segment to
pixel runs at the target resolution.

    python3 gen_segmap.py  assets/Tamagotchi_v3.svg  segmap.c segmap.h

Output format: per segment, a run list of horizontal spans. A segment is a few
dozen runs at most, so the whole map is small enough for an 8 MB watch.
"""

import os
import re
import sys

# Two rendering modes.
#
# SEG_RES (default): scale the artwork to fit a RES x RES box. Smooth, but the
# 2.6-unit inter-dot gap becomes sub-pixel at small sizes and rasterises into a
# visible grid of lines.
#
# SEG_DOT / SEG_GAP: pixel-exact. Every dot becomes exactly SEG_DOT pixels with a
# SEG_GAP pixel gap, i.e. a (SEG_DOT+SEG_GAP) pitch, and the rest of the artwork
# is scaled to match that pitch. No rounding, so no aliasing artefacts.
#
# SEG_DOT=5 SEG_GAP=1 is the default: 292x300, the largest pixel-exact size that
# still fits the T-Watch Ultra's 320x400 framebuffer.
#
# Fitting table, measured against the 1219.7 x 1254.6 artwork box:
#
#     dot  gap   output      margin in 320x400
#      4    1    243 x 250   77 x 150    fits
#      5    1    292 x 300   28 x 100    fits  <- default
#      6    1    340 x 350  -20 x  50    does not fit; desktop only
#
# All three look correct. The choice is preference, not quality: bigger dots are
# easier to read, and the 1 px gap stays crisp at any size because snapping is
# exact, so there is no aliasing tradeoff to trade against.
RES      = int(os.environ.get("SEG_RES", "576"))
DOT_PX   = int(os.environ.get("SEG_DOT", "5"))     # 0 = use SEG_RES mode
GAP_PX   = int(os.environ.get("SEG_GAP", "1"))
BEZIER_SEGMENTS = 6           # flattening resolution for cubic curves


# ---------------------------------------------------------------- path parsing

def _num(tok):
    return float(tok)


def parse_path(d):
    """
    Parse an SVG path into a list of closed polygons (lists of (x, y)).

    Handles M/m L/l H/h V/v C/c S/s Q/q T/t Z/z. Curves are flattened.
    """
    # tokenise: commands and numbers (including exponents and leading signs)
    tokens = re.findall(r'[MmLlHhVvCcSsQqTtZz]|-?\d*\.?\d+(?:[eE][-+]?\d+)?', d)

    polys = []
    cur = []
    x = y = 0.0
    start_x = start_y = 0.0
    cmd = None
    i = 0

    def flush():
        nonlocal cur
        if len(cur) >= 3:
            polys.append(cur)
        cur = []

    while i < len(tokens):
        t = tokens[i]
        if re.match(r'[A-Za-z]', t):
            cmd = t
            i += 1
            if cmd in 'Zz':
                flush()
                x, y = start_x, start_y
                continue
            continue

        if cmd in 'Mm':
            nx, ny = _num(tokens[i]), _num(tokens[i + 1])
            i += 2
            if cmd == 'm':
                nx += x
                ny += y
            flush()
            x, y = nx, ny
            start_x, start_y = x, y
            cur.append((x, y))
            # subsequent coordinate pairs are implicit lineto
            cmd = 'L' if cmd == 'M' else 'l'

        elif cmd in 'Ll':
            nx, ny = _num(tokens[i]), _num(tokens[i + 1])
            i += 2
            if cmd == 'l':
                nx += x
                ny += y
            x, y = nx, ny
            cur.append((x, y))

        elif cmd in 'Hh':
            nx = _num(tokens[i])
            i += 1
            if cmd == 'h':
                nx += x
            x = nx
            cur.append((x, y))

        elif cmd in 'Vv':
            ny = _num(tokens[i])
            i += 1
            if cmd == 'v':
                ny += y
            y = ny
            cur.append((x, y))

        elif cmd in 'Cc':
            pts = [_num(tokens[i + k]) for k in range(6)]
            i += 6
            if cmd == 'c':
                pts = [pts[0] + x, pts[1] + y, pts[2] + x, pts[3] + y,
                       pts[4] + x, pts[5] + y]
            x0, y0 = x, y
            for s in range(1, BEZIER_SEGMENTS + 1):
                u = s / BEZIER_SEGMENTS
                v = 1 - u
                bx = (v ** 3) * x0 + 3 * v * v * u * pts[0] + 3 * v * u * u * pts[2] + u ** 3 * pts[4]
                by = (v ** 3) * y0 + 3 * v * v * u * pts[1] + 3 * v * u * u * pts[3] + u ** 3 * pts[5]
                cur.append((bx, by))
            x, y = pts[4], pts[5]

        elif cmd in 'Ss':
            # reflect previous control point; treat as C with mirrored control
            pts = [_num(tokens[i + k]) for k in range(4)]
            i += 4
            if cmd == 's':
                pts = [pts[0] + x, pts[1] + y, pts[2] + x, pts[3] + y]
            x0, y0 = x, y
            for s in range(1, BEZIER_SEGMENTS + 1):
                u = s / BEZIER_SEGMENTS
                v = 1 - u
                bx = (v ** 3) * x0 + 3 * v * v * u * x0 + 3 * v * u * u * pts[0] + u ** 3 * pts[2]
                by = (v ** 3) * y0 + 3 * v * v * u * y0 + 3 * v * u * u * pts[1] + u ** 3 * pts[3]
                cur.append((bx, by))
            x, y = pts[2], pts[3]

        elif cmd in 'Qq':
            pts = [_num(tokens[i + k]) for k in range(4)]
            i += 4
            if cmd == 'q':
                pts = [pts[0] + x, pts[1] + y, pts[2] + x, pts[3] + y]
            x0, y0 = x, y
            for s in range(1, BEZIER_SEGMENTS + 1):
                u = s / BEZIER_SEGMENTS
                v = 1 - u
                bx = v * v * x0 + 2 * v * u * pts[0] + u * u * pts[2]
                by = v * v * y0 + 2 * v * u * pts[1] + u * u * pts[3]
                cur.append((bx, by))
            x, y = pts[2], pts[3]

        else:
            i += 1          # unknown token; skip

    flush()
    return polys


# ------------------------------------------------------------- rasterisation

def rasterize(polys, x0, y0, scale, res, snap=None):
    """
    Scanline-fill polygons (nonzero winding) into a set of horizontal runs.

    Returns a list of (row, x_start, x_end) with x_end exclusive.

    `snap` = (dot_px, gap_px, ox, oy) forces every closed shape onto an exact
    pixel grid: the shape is drawn as a solid dot_px square whose top-left sits
    at the snapped position. Used for the LCD dot matrix so that the dot size
    and gap are exactly what was asked for rather than whatever rounding gives.
    """
    if snap is not None:
        dot, gap, ox, oy = snap
        pts = [p for poly in polys for p in poly]
        if not pts:
            return []
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        cx = (min(xs) + max(xs)) / 2.0
        cy = (min(ys) + max(ys)) / 2.0
        px = int(round((cx - x0) * scale - dot / 2.0)) + ox
        py = int(round((cy - y0) * scale - dot / 2.0)) + oy
        return [(y, px, px + dot) for y in range(py, py + dot)]

    edges = []
    for poly in polys:
        n = len(poly)
        for i in range(n):
            ax, ay = poly[i]
            bx, by = poly[(i + 1) % n]
            ax = (ax - x0) * scale
            ay = (ay - y0) * scale
            bx = (bx - x0) * scale
            by = (by - y0) * scale
            if ay == by:
                continue
            edges.append((ax, ay, bx, by))

    runs = []
    for row in range(res):
        yc = row + 0.5
        xs = []
        for ax, ay, bx, by in edges:
            if (ay <= yc < by) or (by <= yc < ay):
                t = (yc - ay) / (by - ay)
                xs.append(ax + t * (bx - ax))
        if not xs:
            continue
        xs.sort()
        # nonzero winding: pair them up (simple polygons are fine with even-odd)
        for i in range(0, len(xs) - 1, 2):
            xa = int(round(xs[i]))
            xb = int(round(xs[i + 1]))
            if xb <= xa:
                continue
            if xa < 0:
                xa = 0
            if xb > res:
                xb = res
            if xb > xa:
                runs.append((row, xa, xb))
    return runs


GROUP_OPEN = re.compile(r'<g\b')
GROUP_CLOSE = re.compile(r'</g\s*>')


def _group_span(svg, start):
    """Given the index of a '<g' tag, return the index just past its '</g>'."""
    depth = 0
    i = start
    while i < len(svg):
        o = GROUP_OPEN.search(svg, i)
        c = GROUP_CLOSE.search(svg, i)
        if not o and not c:
            return len(svg)
        if o and (not c or o.start() < c.start()):
            depth += 1
            i = o.end()
        else:
            depth -= 1
            i = c.end()
            if depth == 0:
                return i
    return len(svg)


def extract_segments(svg):
    """
    Collect every element whose id is `byte_bit`.

    Most segments are a single <path>. Three of the status icons (byte 1 bit 3,
    byte 1 bit 2, byte 6 bit 3 -- the game, praise and punish icons) are <g>
    groups containing their whole artwork. Matching only <path> silently dropped
    those three, which is exactly the bug this function exists to avoid.
    """
    segs = {}
    for m in re.finditer(r'id="(\d+)_(\d+)"', svg):
        key = (int(m.group(1)), int(m.group(2)))
        if key in segs:
            continue
        tag_start = svg.rfind('<', 0, m.start())
        tag = svg[tag_start:m.start()]

        if tag.startswith('<path'):
            end = svg.find('>', m.end())
            chunk = svg[tag_start:end + 1]
        elif tag.startswith('<g'):
            end = _group_span(svg, tag_start)
            chunk = svg[tag_start:end]
        else:
            continue

        ds = re.findall(r'\bd="([^"]+)"', chunk)
        if ds:
            segs[key] = ds
    return segs


def main(argv):
    svg_path, out_c, out_h = argv[1], argv[2], argv[3]
    svg = open(svg_path, encoding='utf-8').read()

    segs = extract_segments(svg)

    # Fail loudly on the class of bug that silently dropped three icons: an
    # id of the byte_bit form living on an element type this script does not
    # handle would otherwise just vanish from the map.
    all_ids = set(re.findall(r'id="(\d+_\d+)"', svg))
    got_ids = {"%d_%d" % k for k in segs}
    if all_ids - got_ids:
        print("WARNING: %d element(s) with a segment id were not extracted: %s"
              % (len(all_ids - got_ids), sorted(all_ids - got_ids)),
              file=sys.stderr)

    if not segs:
        print("no segments found", file=sys.stderr)
        return 1

    parsed = {k: [poly for d in ds for poly in parse_path(d)]
              for k, ds in segs.items()}

    # global bounding box over all flattened points
    gx0 = gy0 = 1e9
    gx1 = gy1 = -1e9
    for polys in parsed.values():
        for poly in polys:
            for (px, py) in poly:
                gx0 = min(gx0, px); gy0 = min(gy0, py)
                gx1 = max(gx1, px); gy1 = max(gy1, py)

    w = gx1 - gx0
    h = gy1 - gy0

    # Dot size in artwork units, measured from the artwork itself.
    DOT_UNITS = 22.5
    PITCH_UNITS = 25.1

    if DOT_PX > 0:
        # Pixel-exact mode: fix the scale from the dot pitch, then snap dots.
        scale = float(DOT_PX + GAP_PX) / PITCH_UNITS
        img_w = int(round(w * scale))
        img_h = int(round(h * scale))
        res_w, res_h = img_w, img_h
        snap = (DOT_PX, GAP_PX, 0, 0)
    else:
        scale = min(RES / w, RES / h)
        img_w = int(round(w * scale))
        img_h = int(round(h * scale))
        res_w, res_h = RES, RES
        snap = None

    entries = []
    all_runs = []
    for key in sorted(segs):
        b, n = key
        polys = parsed[key]
        # only the small square dots get snapped; icons keep their shape
        is_dot = False
        if snap is not None:
            pts = [p for poly in polys for p in poly]
            if pts:
                xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
                is_dot = (15 <= max(xs) - min(xs) <= 30 and
                          15 <= max(ys) - min(ys) <= 30)
        runs = rasterize(polys, gx0, gy0, scale, res_w,
                         snap if (snap is not None and is_dot) else None)
        if not runs:
            continue
        entries.append((b, n, len(all_runs), len(runs)))
        all_runs.extend(runs)

    with open(out_c, 'w') as f:
        f.write("/* Generated by gen_segmap.py - do not edit.\n")
        f.write(" * %d segments, %d runs, grid %dx%d\n" % (len(entries), len(all_runs), RES, RES))
        f.write(" * Parsed from the SVG artwork with full path handling (M/L/H/V/C),\n")
        f.write(" * curves flattened, then scanline-rasterised. */\n\n")
        f.write('#include "segmap.h"\n\n')
        f.write("const seg_entry seg_map[SEG_COUNT] = {\n")
        for b, n, start, count in entries:
            f.write("  {%3d,%d,%5d,%4d},\n" % (b, n, start, count))
        f.write("};\n\nconst seg_run seg_runs[SEG_RUN_COUNT] = {\n")
        for (row, xa, xb) in all_runs:
            f.write("  {%3d,%3d,%3d},\n" % (row, xa, xb))
        f.write("};\n")

    with open(out_h, 'w') as f:
        f.write("#ifndef SEGMAP_H\n#define SEGMAP_H\n#include <stdint.h>\n\n")
        f.write("#define SEG_COUNT     %d\n" % len(entries))
        f.write("#define SEG_RUN_COUNT %d\n" % len(all_runs))
        f.write("#define SEG_W         %d\n#define SEG_H         %d\n" % (res_w, res_h))
        f.write("#define SEG_BBOX_X0   %.3f\n#define SEG_BBOX_Y0   %.3f\n" % (gx0, gy0))
        f.write("#define SEG_BBOX_W    %.3f\n#define SEG_BBOX_H    %.3f\n" % (w, h))
        f.write("#define SEG_SCALE     %.9f\n\n" % scale)
        f.write("typedef struct { uint8_t byte, bit; uint16_t run_start, run_count; } seg_entry;\n")
        f.write("typedef struct { uint16_t y, x0, x1; } seg_run;\n\n")
        f.write("extern const seg_entry seg_map[SEG_COUNT];\n")
        f.write("extern const seg_run   seg_runs[SEG_RUN_COUNT];\n")
        f.write("#endif\n")

    mode = ("dot %dpx gap %dpx" % (DOT_PX, GAP_PX)) if DOT_PX else ("fit %dpx" % RES)
    print("segments %d, runs %d, output %dx%d, scale %.6f, mode %s" %
          (len(entries), len(all_runs), res_w, res_h, scale, mode))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
