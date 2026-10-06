#!/usr/bin/env python3
"""Generate the 128x128 subterranean layer beneath assets/maps/surface.map.

The layer keeps the surface coordinates: tile (x, y) below is the ground
under tile (x, y) above. It is built from the documented underground of the
area (see docs/maps.md for the sources) and from the street map itself:

  * every node of tools/nodes.py is a shaft; the manhole above and the
    staircase below share the same tile (tile type 2 in both layers);
  * the aqueduct galleries (Carmignano under the Quartieri Spagnoli, Bolla
    under the lower city) join their nodes along shortest paths *through
    the streets above*, because the channels fed the wells street by street;
  * the Linea 1 running tunnel (a building site in 2010) joins the Toledo
    and Municipio shafts and leaves east towards Piazza Bovio;
  * the Funicolare Centrale inclined tunnel climbs from the Augusteo to
    Corso Vittorio Emanuele and leaves west towards the Vomero;
  * cisterns, the tuff quarry, the air-raid shelter, the Bourbon tunnel
    stub, the Salone Margherita, the Roman harbour dig, the castle dungeon
    and the crypt are dug at the foot of their shafts.

The surface map receives its manholes here too ('O'), so run this after
tools/build_map.py. Deterministic: no random numbers, only a coordinate hash.

Usage: tools/build_underground.py [--preview build/underground.png]
"""
from __future__ import annotations

import argparse
import sys
from collections import deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from nodes import NODES, WELLS  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
N = 128
SURFACE_WALK = ".:+gGO"
UNDER_WALK = ".,w=sO"


def hash2(x: int, y: int) -> int:
    h = (x * 73856093) ^ (y * 19349663)
    h ^= h >> 13
    return (h * 0x5BD1E995) & 0xFFFFFFFF


def load(path: Path) -> list[list[str]]:
    return [list(line) for line in path.read_text(encoding="ascii").splitlines()]


def snap(surface, x, y, taken):
    """Nearest plain street tile with room for the shaft."""
    best = None
    for r in range(0, 8):
        for yy in range(y - r, y + r + 1):
            for xx in range(x - r, x + r + 1):
                if 2 <= xx < N - 2 and 2 <= yy < N - 2 and surface[yy][xx] in ".+:" \
                        and all(abs(xx - tx) + abs(yy - ty) > 3 for tx, ty in taken):
                    d = (xx - x) ** 2 + (yy - y) ** 2
                    if best is None or d < best[0]:
                        best = (d, xx, yy)
        if best:
            return best[1], best[2]
    raise SystemExit(f"no street near {x},{y}")


def street_path(surface, a, b):
    """Shortest 4-connected path from a to b through the surface streets."""
    prev = {a: None}
    q = deque([a])
    while q:
        cur = q.popleft()
        if cur == b:
            break
        x, y = cur
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 < nx < N - 1 and 0 < ny < N - 1 and (nx, ny) not in prev and surface[ny][nx] in SURFACE_WALK:
                prev[(nx, ny)] = cur
                q.append((nx, ny))
    if b not in prev:
        raise SystemExit(f"streets do not join {a} and {b}")
    out, cur = [], b
    while cur is not None:
        out.append(cur)
        cur = prev[cur]
    return out


def line(a, b):
    (x0, y0), (x1, y1) = a, b
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err, out = dx + dy, []
    while True:
        out.append((x0, y0))
        if (x0, y0) == (x1, y1):
            return out
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def build(surface):
    under = [["#"] * N for _ in range(N)]
    inside = lambda x, y: 1 <= x < N - 1 and 1 <= y < N - 1

    def put(x, y, t, over="#"):
        if inside(x, y) and under[y][x] in over:
            under[y][x] = t

    def room(cx, cy, w, h, floor=",", water=0, pillars=0, chasm=0, wall=None):
        x0, y0 = cx - w // 2, cy - h // 2
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                if not inside(x, y):
                    continue
                edge = x in (x0, x0 + w - 1) or y in (y0, y0 + h - 1)
                t = floor
                if not edge:
                    if chasm and abs(x - cx) < chasm and abs(y - cy - 1) < chasm:
                        t = "~"
                    elif water and (y - y0) >= h - 1 - water:
                        t = "w"
                    elif pillars and (x - x0) % pillars == pillars // 2 and (y - y0) % pillars == pillars // 2:
                        t = "I"
                put(x, y, t, over="#M.")
        if wall:
            for y in range(y0 - 1, y0 + h + 1):
                for x in range(x0 - 1, x0 + w + 1):
                    put(x, y, wall)

    taken, shafts = [], {}
    for key, _name, x, y, _kind, _net in NODES:
        sx, sy = snap(surface, x, y, taken)
        taken.append((sx, sy))
        shafts[key] = (sx, sy)

    # Rooms at the foot of the shafts. The shaft tile itself stays a floor so
    # that the staircase always has walkable ground around it.
    for key, _name, _x, _y, kind, _net in NODES:
        x, y = shafts[key]
        if kind == "well":
            room(x, y, 3, 3)
        elif kind == "cistern":
            room(x + 1, y + 2, 7, 6, water=2, pillars=3)
        elif kind == "quarry":            # bell-shaped tuff quarry with a deep pit
            room(x, y + 3, 11, 9, chasm=2, pillars=4)
        elif kind == "shelter":           # air-raid shelter beside the cisterns
            room(x - 1, y + 3, 9, 7, floor="s", pillars=4)
            room(x + 8, y + 3, 7, 7, water=3, chasm=0)
            for xx in range(x + 3, x + 6):
                put(xx, y + 3, ".", over="#s,")
        elif kind == "bourbon":           # the Bourbon tunnel leaves south
            room(x, y + 2, 5, 5, wall="M")
            for yy in range(y + 4, N - 1):
                for xx in (x - 1, x, x + 1):
                    put(xx, yy, "." if xx == x else "M", over="#M")
        elif kind == "salone":
            room(x, y + 2, 7, 5, floor="s", wall="M")
        elif kind == "port":              # Roman harbour dig: quay, water, hulls
            room(x + 2, y + 3, 11, 8, water=4, wall="M")
        elif kind == "dungeon":           # the pit under the castle
            room(x + 1, y + 2, 7, 7, chasm=2, wall="M")
        elif kind == "crypt":
            room(x, y + 2, 7, 5, pillars=3, wall="M")
        elif kind in ("metro", "funicular"):
            room(x, y + 1, 5, 4, wall="M")

    def gallery(cells, t=".", width=1, wall=None):
        for x, y in cells:
            for dy in range(width):
                for dx in range(width):
                    put(x + dx, y + dy, t, over="#M")
        if wall:
            for x, y in cells:
                for dy in range(-1, width + 1):
                    for dx in range(-1, width + 1):
                        put(x + dx, y + dy, wall)

    # Aqueduct galleries under the streets, joining each network as a chain
    # of nearest neighbours (a small spanning tree).
    for net in ("carmignano", "bolla"):
        todo = [k for k, *_r, n in NODES if n == net]
        done = [todo.pop(0)]
        while todo:
            d, a, b = min((abs(shafts[a][0] - shafts[b][0]) + abs(shafts[a][1] - shafts[b][1]), a, b)
                          for a in done for b in todo)
            gallery(street_path(surface, shafts[a], shafts[b]))
            done.append(b)
            todo.remove(b)
    # Blind wells: a cistern joined to the nearest shaft of its network.
    for wx, wy, net in WELLS:
        x, y = snap(surface, wx, wy, taken)
        taken.append((x, y))
        room(x, y + 1, 5, 4, water=1 + hash2(x, y) % 2, pillars=0)
        _d, key = min((abs(shafts[k][0] - x) + abs(shafts[k][1] - y), k) for k, *_r, n in NODES if n == net)
        gallery(street_path(surface, (x, y), shafts[key]))
    # Loops, so that the galleries are a network and not a tree.
    for a, b in (("montecalv", "concordia"), ("concordia", "santanna"), ("trinita", "santanna"),
                 ("scura", "trinita"), ("medina", "maschio"), ("catalana", "monteoliv")):
        gallery(street_path(surface, shafts[a], shafts[b]))
    # Cross links between the networks, again under real streets.
    for a, b in (("carita", "toledo"), ("augusteo", "trinita"), ("galleria", "augusteo"),
                 ("municipio", "medina"), ("galleria", "municipio"), ("carita", "monteoliv")):
        gallery(street_path(surface, shafts[a], shafts[b]))

    # Linea 1 running tunnel: Toledo - Municipio - (Piazza Bovio, off map).
    t, m = shafts["toledo"], shafts["municipio"]
    gallery(line((t[0], 2), (t[0], t[1])), "=", 2, "M")          # from Dante, north
    gallery(line(t, m), "=", 2, "M")
    gallery(line(m, (N - 3, 46)), "=", 2, "M")
    # Funicolare Centrale: Augusteo - Corso Vittorio Emanuele - (Vomero).
    a, c = shafts["augusteo"], shafts["corsove"]
    gallery(line(a, c), "=", 1, "M")
    gallery(line(c, (2, c[1] - 6)), "=", 1, "M")

    # Small cisterns under the palazzi along the galleries: one every few
    # tiles, on the side where the surface is a block.
    for y in range(4, N - 4):
        for x in range(4, N - 4):
            if under[y][x] == "." and hash2(x, y) % 11 == 0:
                for dx, dy in ((0, -1), (1, 0), (0, 1), (-1, 0)):
                    cx, cy = x + dx * 2, y + dy * 2
                    block = all(inside(cx + i, cy + j) and surface[cy + j][cx + i] in "#P"
                                and under[cy + j][cx + i] == "#"
                                for j in (-1, 0, 1) for i in (-1, 0, 1))
                    if block:
                        room(cx, cy, 3, 3, water=hash2(y, x) % 2)
                        break

    # Staircases, with guaranteed floor beside and below them. The tile to
    # the north is left alone: where it is rock the stairs climb into the
    # wall, under their torch, and walking up from below stops on them.
    for key, (x, y) in shafts.items():
        for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1)):
            if under[y + dy][x + dx] in "#MI~":
                under[y + dy][x + dx] = "."
        under[y][x] = "O"
        surface[y][x] = "O"

    # Torches: a rock tile above a floor, spaced by the coordinate hash, and
    # always one beside each staircase.
    for y in range(1, N - 1):
        for x in range(1, N - 1):
            if under[y][x] in "#M" and under[y + 1][x] in UNDER_WALK and hash2(x, y) % 9 == 0:
                under[y][x] = "t"
    for x, y in shafts.values():
        for dx in (0, -1, 1):
            if under[y - 1][x + dx] in "#M":
                under[y - 1][x + dx] = "t"
                break
    return under, shafts


def check(surface, under, shafts):
    """Both layers together must be one connected world."""
    start = next(iter(shafts.values()))
    seen = {(0, *start)}
    q = deque(seen)
    while q:
        layer, x, y = q.popleft()
        grid, walk = (surface, SURFACE_WALK) if layer == 0 else (under, UNDER_WALK)
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < N and 0 <= ny < N and grid[ny][nx] in walk and (layer, nx, ny) not in seen:
                seen.add((layer, nx, ny))
                q.append((layer, nx, ny))
        if grid[y][x] == "O" and (1 - layer, x, y) not in seen:
            seen.add((1 - layer, x, y))
            q.append((1 - layer, x, y))
    for layer, grid, walk in ((0, surface, SURFACE_WALK), (1, under, UNDER_WALK)):
        total = sum(1 for y in range(N) for x in range(N) if grid[y][x] in walk)
        reach = sum(1 for l, _x, _y in seen if l == layer)
        print(f"layer {layer}: {reach}/{total} walkable tiles reachable")
        if reach != total:
            # Unreachable pockets become solid so that no task can hide there.
            for y in range(N):
                for x in range(N):
                    if grid[y][x] in walk and (layer, x, y) not in seen:
                        grid[y][x] = "#"


PREVIEW = {"#": (30, 28, 44), "M": (70, 70, 84), ".": (150, 120, 70), ",": (176, 146, 90), "w": (40, 90, 170),
           "=": (120, 120, 130), "I": (90, 76, 60), "~": (0, 0, 0), "t": (255, 170, 30), "s": (150, 150, 120),
           "O": (255, 60, 60)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preview", type=Path)
    args = ap.parse_args()
    spath, upath = ROOT / "assets/maps/surface.map", ROOT / "assets/maps/underground.map"
    surface = load(spath)
    for row in surface:                       # manholes are re-derived on every run
        for i, c in enumerate(row):
            if c == "O":
                row[i] = "."
    under, shafts = build(surface)
    check(surface, under, shafts)
    spath.write_text("\n".join("".join(r) for r in surface) + "\n", encoding="ascii")
    upath.write_text("\n".join("".join(r) for r in under) + "\n", encoding="ascii")
    print(f"wrote {upath}: " + " ".join(f"{c}={sum(r.count(c) for r in under)}" for c in PREVIEW))
    for key, (x, y) in shafts.items():
        print(f"  shaft {key:10s} {x:3d},{y:3d}")
    if args.preview:
        from PIL import Image
        im = Image.new("RGB", (N, N))
        im.putdata([PREVIEW[c] for r in under for c in r])
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        im.resize((N * 6, N * 6), Image.NEAREST).save(args.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
