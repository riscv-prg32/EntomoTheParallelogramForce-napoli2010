#!/usr/bin/env python3
"""Derive the 128x128 surface tile map from the Tuttocitta' Napoli street atlas.

Source: "Tuttocitta' Napoli" (SEAT Pagine Gialle), PDF pages 42-45, that is
plates Tav. 35, 36, 37 and 38. The four plates share one scale and abut
exactly; every plate is a 4x5 grid of squares (columns A-D, rows 1-5).

The game window is 8x8 atlas squares:

    columns  Tav.35/37 A-D + Tav.36/38 A-D
    rows     Tav.35/36 rows 2-5 + Tav.37/38 rows 1-4

and every atlas square becomes exactly 16x16 game tiles (about 9.4 m per
tile), so the in-game grid reference (for example "TAV 35 C3") is the real
one. The atlas itself is copyrighted and is NOT part of this repository:
only the derived 128x128 grid in assets/maps/surface.map is committed.

Method (deterministic):
  1. render the four pages without text (Ghostscript -dFILTERTEXT), crop the
     map frames and stitch them;
  2. classify every pixel by its flat cartographic colour (street, pedestrian
     area, block, building of public interest, green, water);
  3. thin the street mask to its centre lines (Zhang-Suen) so that a vicolo
     narrower than a tile still becomes a one-tile path;
  4. decide every tile from the centre lines and the class fractions, repair
     diagonal-only joints, and keep the largest connected street network.

Usage: tools/build_map.py /path/to/TUT_NAPOLI.pdf  [--preview build/surface.png]
Needs Ghostscript (gs), numpy and Pillow.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from collections import deque
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
DPI = 200
CELL_W, CELL_H = 236, 233            # one atlas square at 200 dpi
FIRST_H_LINE = 445                   # y of the first horizontal grid line
FIRST_V_LINE = {42: 401, 43: 385, 44: 401, 45: 385}   # left/right page layouts
PAGES = (42, 43, 44, 45)             # Tav. 35, 36, 37, 38
N = 128

COLOURS = {                          # flat colours of the atlas
    "block": (253, 226, 195), "public": (229, 200, 172), "street": (255, 255, 255),
    "edge": (147, 149, 152), "pedestrian": (255, 245, 132), "green": (178, 210, 117),
    "water": (171, 225, 250), "grid": (236, 0, 140),
}


def render(pdf: Path, tmp: Path) -> Image.Image:
    subprocess.run(["gs", "-q", "-dNOPAUSE", "-dBATCH", "-dFILTERTEXT", "-sDEVICE=png16m",
                    f"-r{DPI}", f"-dFirstPage={PAGES[0]}", f"-dLastPage={PAGES[-1]}",
                    f"-sOutputFile={tmp}/p-%d.png", str(pdf)], check=True, capture_output=True)
    w, h = 4 * CELL_W, 5 * CELL_H
    mosaic = Image.new("RGB", (2 * w, 2 * h))
    for k, page in enumerate(PAGES):
        im = Image.open(tmp / f"p-{k + 1}.png").convert("RGB")
        x0, y0 = FIRST_V_LINE[page] - CELL_W, FIRST_H_LINE - CELL_H
        mosaic.paste(im.crop((x0, y0, x0 + w, y0 + h)), ((k % 2) * w, (k // 2) * h))
    window = np.array(mosaic.crop((0, CELL_H, 8 * CELL_W, 9 * CELL_H)))
    # The plate frames leave a thin line on the two seams: cover each seam
    # with the pixels just beside it.
    sx, sy = 4 * CELL_W, 4 * CELL_H
    window[:, sx - 3:sx + 3] = window[:, sx - 9:sx - 3]
    window[sy - 3:sy + 3, :] = window[sy - 9:sy - 3, :]
    return Image.fromarray(window)


def classify(img: Image.Image) -> np.ndarray:
    a = np.asarray(img).astype(np.int32)
    names = list(COLOURS)
    dist = np.stack([((a - np.array(COLOURS[n])) ** 2).sum(-1) for n in names])
    cls = dist.argmin(0)
    # Grid lines and leftover dark strokes carry no land use: borrow the class
    # of the pixel 3 px to the left/up (the lines are 1-2 px wide).
    unknown = (cls == names.index("grid")) | (dist.min(0) > 3000)
    for shift, axis in ((3, 1), (3, 0), (-3, 1), (-3, 0)):
        cls = np.where(unknown, np.roll(cls, shift, axis), cls)
        unknown &= (cls == names.index("grid"))
    return cls, names


def thin(mask: np.ndarray) -> np.ndarray:
    """Zhang-Suen thinning, vectorised."""
    img = np.pad(mask.astype(np.uint8), 1)
    while True:
        changed = False
        for step in (0, 1):
            p2, p3, p4 = img[:-2, 1:-1], img[:-2, 2:], img[1:-1, 2:]
            p5, p6, p7 = img[2:, 2:], img[2:, 1:-1], img[2:, :-2]
            p8, p9 = img[1:-1, :-2], img[:-2, :-2]
            ring = [p2, p3, p4, p5, p6, p7, p8, p9, p2]
            b = sum(ring[:8])
            a = sum(((ring[i] == 0) & (ring[i + 1] == 1)).astype(np.uint8) for i in range(8))
            if step == 0:
                cond = (p2 * p4 * p6 == 0) & (p4 * p6 * p8 == 0)
            else:
                cond = (p2 * p4 * p8 == 0) & (p2 * p6 * p8 == 0)
            kill = (img[1:-1, 1:-1] == 1) & (b >= 2) & (b <= 6) & (a == 1) & cond
            if kill.any():
                img[1:-1, 1:-1][kill] = 0
                changed = True
        if not changed:
            return img[1:-1, 1:-1].astype(bool)


def tiles_from(cls: np.ndarray, names: list[str]) -> list[list[str]]:
    h, w = cls.shape
    idx = {n: names.index(n) for n in names}
    paved = (cls == idx["street"]) | (cls == idx["pedestrian"])
    # The grey casing is shared by streets and by the footprints drawn inside
    # the buildings of public interest: it is street only next to paving.
    near = paved.copy()
    for d in (1, 2):
        for axis in (0, 1):
            near |= np.roll(paved, d, axis) | np.roll(paved, -d, axis)
    walk = paved | ((cls == idx["edge"]) & near)
    centre = thin(walk)
    ys = [round(i * h / N) for i in range(N + 1)]
    xs = [round(i * w / N) for i in range(N + 1)]
    grid = [["#"] * N for _ in range(N)]
    frac_walk = np.zeros((N, N))
    for ty in range(N):
        for tx in range(N):
            sl = (slice(ys[ty], ys[ty + 1]), slice(xs[tx], xs[tx + 1]))
            c = cls[sl]
            n = c.size
            f = {k: (c == v).sum() / n for k, v in idx.items()}
            fw = walk[sl].sum() / n
            frac_walk[ty, tx] = fw
            line = centre[sl].sum()
            if f["water"] > 0.5:
                t = "~"
            elif line >= 5 or fw >= 0.55:
                if fw >= 0.85:
                    t = "+"                                   # piazza / wide carriageway
                elif f["pedestrian"] > 0.5 * fw:
                    t = ":"                                   # pedestrian area
                else:
                    t = "."
            elif f["green"] > 0.5:
                t = "g"
            elif f["public"] > f["block"]:
                t = "P"
            else:
                t = "#"
            grid[ty][tx] = t
    walkable = lambda t: t in ".:+"
    # Diagonal-only joints: open the orthogonal neighbour with more street.
    for _ in range(2):
        for ty in range(N - 1):
            for tx in range(N - 1):
                a, b = grid[ty][tx], grid[ty][tx + 1]
                c, d = grid[ty + 1][tx], grid[ty + 1][tx + 1]
                if walkable(a) and walkable(d) and not walkable(b) and not walkable(c):
                    if frac_walk[ty, tx + 1] >= frac_walk[ty + 1, tx]:
                        grid[ty][tx + 1] = "."
                    else:
                        grid[ty + 1][tx] = "."
                if walkable(b) and walkable(c) and not walkable(a) and not walkable(d):
                    if frac_walk[ty, tx] >= frac_walk[ty + 1, tx + 1]:
                        grid[ty][tx] = "."
                    else:
                        grid[ty + 1][tx + 1] = "."
    # The galleria: pedestrian tiles enclosed by a building of public interest.
    for ty in range(1, N - 1):
        for tx in range(1, N - 1):
            if grid[ty][tx] in ":.":
                around = [grid[ty + dy][tx + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1)]
                if around.count("P") >= 4 and around.count("#") == 0:
                    grid[ty][tx] = "G"
    # Green: grass next to a street is walkable, the inside of a park is trees.
    for ty in range(N):
        for tx in range(N):
            if grid[ty][tx] == "g":
                near = any(0 <= ty + dy < N and 0 <= tx + dx < N and grid[ty + dy][tx + dx] in ".:+G"
                           for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)))
                if not near:
                    grid[ty][tx] = "T"
    # The map border is closed, and only the largest street network is kept.
    for i in range(N):
        for ty, tx in ((0, i), (N - 1, i), (i, 0), (i, N - 1)):
            if grid[ty][tx] in ".:+gG":
                grid[ty][tx] = "#"
    open_ = lambda t: t in ".:+gG"
    seen = [[0] * N for _ in range(N)]
    best, label = (0, 0), 0
    for sy in range(N):
        for sx in range(N):
            if open_(grid[sy][sx]) and not seen[sy][sx]:
                label += 1
                size, q = 0, deque([(sy, sx)])
                seen[sy][sx] = label
                while q:
                    y, x = q.popleft()
                    size += 1
                    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                        yy, xx = y + dy, x + dx
                        if 0 <= yy < N and 0 <= xx < N and open_(grid[yy][xx]) and not seen[yy][xx]:
                            seen[yy][xx] = label
                            q.append((yy, xx))
                best = max(best, (size, label))
    for y in range(N):
        for x in range(N):
            if open_(grid[y][x]) and seen[y][x] != best[1]:
                grid[y][x] = "T" if grid[y][x] == "g" else "#"
    return grid


PREVIEW = {"#": (222, 176, 120), "P": (196, 130, 100), ".": (120, 120, 132), ":": (214, 200, 120),
           "+": (150, 150, 160), "g": (120, 170, 80), "T": (60, 120, 60), "~": (80, 150, 220),
           "G": (230, 230, 240), "O": (255, 0, 0)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pdf", type=Path)
    ap.add_argument("--out", type=Path, default=ROOT / "assets/maps/surface.map")
    ap.add_argument("--preview", type=Path)
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix="entomo-map-") as tmp:
        window = render(args.pdf, Path(tmp))
    cls, names = classify(window)
    grid = tiles_from(cls, names)
    args.out.write_text("\n".join("".join(r) for r in grid) + "\n", encoding="ascii")
    counts = {c: sum(r.count(c) for r in grid) for c in PREVIEW}
    print(f"wrote {args.out}: " + " ".join(f"{c}={n}" for c, n in counts.items() if n))
    if args.preview:
        im = Image.new("RGB", (N, N))
        im.putdata([PREVIEW[c] for r in grid for c in r])
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        im.resize((N * 6, N * 6), Image.NEAREST).save(args.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
