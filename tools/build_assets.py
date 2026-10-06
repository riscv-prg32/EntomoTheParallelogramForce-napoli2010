#!/usr/bin/env python3
"""Generate src/gen_assets.h: palettes, maps, tiles, sprites, places, tasks.

Everything the cartridge draws is produced here from formulas and from the
ASCII pixel art below; there are no external image files. Inputs are the
two committed maps (assets/maps/*.map) and tools/nodes.py.

Layout rules that the C side relies on:
  * colour = palette * 16 + index; palettes are 0 surface tiles,
    1 underground tiles, 2 hero and effects, 3 actors and objects,
    4 backdrop / foreground / ledger, 5 underground tiles dimmed;
  * maps are nibble-packed, low nibble = even x;
  * tiles and sprites are 4 bits per pixel, high nibble = left pixel;
  * no pointers: every table is numbers or fixed-size char arrays, because
    portable cartridges carry no relocations.

Usage: tools/build_assets.py [--sheet build/sheet.png]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from nodes import GAZETTEER, NODES, ZONES  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
N = 128

# --------------------------------------------------------------- palettes
PALETTES = [
    # 0: surface tiles
    [(52, 54, 70), (80, 82, 100), (110, 112, 130), (156, 150, 142),
     (150, 100, 56), (206, 156, 92), (238, 200, 128), (250, 228, 172),
     (184, 88, 50), (126, 50, 40), (34, 28, 50), (36, 98, 58),
     (98, 164, 70), (216, 190, 108), (242, 242, 232), (198, 70, 62)],
    # 1: underground tiles
    [(4, 4, 12), (14, 18, 46), (26, 38, 88), (54, 76, 146),
     (40, 34, 54), (72, 58, 72), (124, 92, 58), (180, 138, 74),
     (255, 176, 40), (255, 226, 124), (204, 96, 24), (28, 84, 156),
     (84, 154, 214), (104, 104, 118), (172, 176, 190), (58, 112, 80)],
    # 2: hero and effects (0 = transparent)
    [(255, 0, 255), (14, 14, 34), (38, 38, 78), (112, 192, 182),
     (118, 138, 100), (166, 186, 138), (206, 222, 176), (232, 184, 150),
     (150, 152, 164), (122, 72, 40), (60, 255, 100), (196, 255, 204),
     (16, 152, 60), (255, 255, 255), (255, 238, 92), (22, 20, 30)],
    # 3: actors and objects (0 = transparent)
    [(255, 0, 255), (26, 20, 30), (226, 172, 134), (86, 52, 36),
     (206, 48, 50), (52, 78, 152), (246, 246, 240), (238, 222, 50),
     (132, 132, 142), (46, 44, 54), (232, 60, 190), (60, 202, 232),
     (70, 172, 70), (222, 122, 50), (150, 104, 70), (255, 150, 192)],
    # 4: backdrop, foreground and ledger (0 = transparent)
    [(255, 0, 255), (20, 24, 66), (62, 50, 112), (152, 84, 112),
     (242, 152, 92), (44, 36, 72), (74, 62, 104), (20, 52, 112),
     (62, 112, 172), (238, 224, 160), (208, 190, 120), (92, 70, 40),
     (222, 40, 140), (238, 238, 246), (212, 60, 72), (72, 122, 212)],
]
PALETTES.append([tuple(c * 9 // 20 for c in rgb) for rgb in PALETTES[1]])   # 5: dimmed


def rgb565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def cube_cell(c565):
    """System-palette index the firmware quantises an RGB565 colour to."""
    named = [0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0x07FF, 0xF81F]
    if c565 in named:
        return named.index(c565)
    r = ((c565 >> 11) & 31) * 5 // 31
    g = ((c565 >> 5) & 63) * 5 // 63
    b = (c565 & 31) * 5 // 31
    return 16 + r * 36 + g * 6 + b


def hash2(x, y):
    h = (x * 73856093) ^ (y * 19349663)
    h ^= h >> 13
    return (h * 0x5BD1E995) & 0xFFFFFFFF


# ------------------------------------------------------------------ tiles
def blank(c=0):
    return [[c] * 16 for _ in range(16)]


def noise(t, colours, seed, density=5):
    for y in range(16):
        for x in range(16):
            h = hash2(x + seed * 31, y + seed * 17)
            if h % density == 0:
                t[y][x] = colours[(h >> 8) % len(colours)]
    return t


def t_basalt(seed, crack=False):
    """Large lava-stone slabs (basoli) in running bond."""
    t = blank(1)
    noise(t, [1, 1, 2, 0], seed, 4)
    for y in range(16):
        for x in range(16):
            row = y // 8
            if y % 8 == 7 or (x + row * 5 + seed * 3) % 10 == 9:
                t[y][x] = 0
            elif y % 8 == 0 and hash2(x, row + seed) % 3:
                t[y][x] = 2
    if crack:
        for i, (x, y) in enumerate([(3, 2), (4, 3), (5, 3), (6, 4), (7, 5), (7, 6), (8, 7), (9, 9), (10, 10), (11, 10)]):
            t[y][x] = 0
            if i % 3 == 0:
                t[y + 1][x] = 2
    return t


def t_tombino():
    t = t_basalt(2)
    for y in range(16):
        for x in range(16):
            d = (x - 7.5) ** 2 + (y - 7.5) ** 2
            if d < 36:
                t[y][x] = 0 if d > 27 else (2 if (x + y) % 3 == 0 else 1)
                if d < 27 and (x in (5, 10) or y in (5, 10)):
                    t[y][x] = 3
    t[7][7] = t[8][8] = t[7][8] = t[8][7] = 0
    return t


def t_piazza(seed=0):
    t = blank(2)
    noise(t, [2, 3, 2, 1], seed + 5, 3)
    for y in range(16):
        for x in range(16):
            if y % 8 == 7 or (x + (y // 8) * 8) % 16 == 15:
                t[y][x] = 1
    return t


def t_pedestrian():
    t = blank(13)
    noise(t, [13, 3, 6], 9, 4)
    for y in range(16):
        for x in range(16):
            if (x % 8 == 7) or (y % 8 == 7):
                t[y][x] = 3
    return t


def t_roof(kind):
    if kind == 0:          # flat tuff terrace with parapet shadow
        t = blank(5)
        noise(t, [5, 6, 4], 21, 4)
        for i in range(16):
            t[0][i] = 6
            t[15][i] = 4
            t[i][0] = 6
            t[i][15] = 4
    elif kind == 1:        # terracotta tiles
        t = blank(8)
        for y in range(16):
            for x in range(16):
                if y % 4 == 3:
                    t[y][x] = 9
                elif (x + (y // 4) * 2) % 4 == 0:
                    t[y][x] = 9 if y % 4 == 2 else 8
                elif y % 4 == 0:
                    t[y][x] = 4
    else:                  # terrace with a water tank and an aerial
        t = t_roof(0)
        for y in range(4, 10):
            for x in range(3, 9):
                t[y][x] = 3 if y > 4 and x > 3 else 14
        for y in range(2, 13):
            t[y][12] = 10
        for x in range(10, 15):
            t[3][x] = 10
            t[5][x] = 10 if x % 2 else t[5][x]
    return t


def t_facade(kind):
    """South face of a tuff palazzo: 0 window, 1 door (basso), 2 balcony, 3 shop."""
    t = blank(6)
    noise(t, [6, 7, 5], 40 + kind, 4)
    for x in range(16):
        t[0][x] = 4                      # eaves shadow
        t[1][x] = 5
        t[15][x] = 4                     # plinth
        t[14][x] = 5
    if kind == 0:
        for y in range(4, 12):
            for x in range(5, 11):
                t[y][x] = 10 if 6 <= x <= 9 and y > 4 else 7
        for y in range(5, 12):
            t[y][6] = 11
            t[y][9] = 11
    elif kind == 1:
        for y in range(5, 16):
            for x in range(4, 12):
                t[y][x] = 10 if y > 5 and 5 <= x <= 10 else 4
        for y in range(8, 16):
            for x in range(6, 8):
                t[y][x] = 9
        t[11][9] = 13
    elif kind == 2:
        for y in range(3, 10):
            for x in range(5, 11):
                t[y][x] = 10 if 6 <= x <= 9 else 7
        for x in range(3, 13):
            t[10][x] = 10
            t[12][x] = 10
            if x % 2:
                t[11][x] = 10
        for x, c in ((4, 14), (5, 14), (7, 15), (8, 15), (10, 12), (11, 12)):
            t[13][x] = c
            t[14][x] = c
    else:
        for y in range(6, 14):
            for x in range(2, 14):
                t[y][x] = 10
        for x in range(1, 15):
            t[4][x] = 15 if x % 4 < 2 else 14
            t[5][x] = 15 if x % 4 < 2 else 14
        for y in range(8, 13):
            for x in range(4, 12):
                t[y][x] = 13 if (x + y) % 3 else 6
    return t


def t_public_roof():
    t = blank(15)
    for y in range(16):
        for x in range(16):
            if (x + y) % 8 == 0 or (x - y) % 8 == 0:
                t[y][x] = 9
            elif (x + y) % 8 == 1:
                t[y][x] = 8
    return t


def t_public_facade():
    t = blank(14)
    noise(t, [14, 3, 7], 77, 5)
    for x in range(16):
        t[0][x] = 9
        t[1][x] = 15
        t[2][x] = 3
        t[15][x] = 3
    for cx in (4, 11):                     # two arches
        for y in range(5, 15):
            for x in range(cx - 2, cx + 3):
                if y > 6 or abs(x - cx) < 2:
                    t[y][x] = 10
    for y in range(3, 15):
        for x in (0, 7, 8, 15):
            t[y][x] = 3
    return t


def t_grass():
    t = blank(12)
    noise(t, [12, 11, 12, 13], 3, 3)
    return t


def t_tree():
    t = t_grass()
    for y in range(16):
        for x in range(16):
            d = (x - 7.5) ** 2 + (y - 6.5) ** 2
            if d < 40:
                t[y][x] = 11 if (d > 24 or (x + 2 * y) % 5 == 0) else 12
            if d < 10 and (x + y) % 3 == 0:
                t[y][x] = 13
    for y in range(12, 16):
        t[y][7] = 4
        t[y][8] = 9
    return t


def t_galleria():
    t = blank(14)
    for y in range(16):
        for x in range(16):
            if (x // 4 + y // 4) % 2:
                t[y][x] = 3
            if abs(x - 7.5) + abs(y - 7.5) < 3:
                t[y][x] = 15 if abs(x - 7.5) + abs(y - 7.5) > 1.5 else 13
    return t


def t_sea():
    return blank(10)


def u_floor(seed, c=(6, 6, 7, 5)):
    t = blank(c[0])
    noise(t, list(c), seed, 3)
    return t


def u_stairs():
    t = u_floor(1)
    for y in range(2, 14):
        for x in range(3, 13):
            step = (y - 2) // 3
            t[y][x] = [7, 6, 5, 4][step] if (y - 2) % 3 else 0
    for y in range(2, 14):
        t[y][2] = 9
        t[y][13] = 8
    return t


def u_water():
    t = blank(11)
    for y in range(16):
        for x in range(16):
            if (x + y * 3) % 8 == 0:
                t[y][x] = 12
            elif (x + y * 3) % 8 == 1 and y % 2:
                t[y][x] = 3
            elif (x * 3 - y) % 11 == 0:
                t[y][x] = 2
    return t


def u_rock(seed):
    t = blank(4)
    noise(t, [4, 5, 4, 1], seed, 3)
    for y in range(16):
        for x in range(16):
            if (x * 2 + y + seed * 5) % 13 == 0:
                t[y][x] = 1
    return t


def u_rockface(torch=False):
    """Tuff wall seen from the gallery: tool marks, lit from below."""
    t = blank(6)
    for y in range(16):
        for x in range(16):
            band = (y + (x // 5) * 2) % 5
            t[y][x] = 5 if y < 3 else (6 if band else 5)
            if y > 11 and (x + y) % 4:
                t[y][x] = 7
            if (x * 7 + y * 3) % 17 == 0:
                t[y][x] = 4
    for x in range(16):
        t[0][x] = 4
        t[15][x] = 5
    if torch:
        for y in range(8, 13):
            t[y][7] = 13
            t[y][8] = 14
        for y in range(2, 9):
            for x in range(5, 11):
                d = abs(x - 7.5) + abs(y - 6) * 0.8
                if d < 3.2:
                    t[y][x] = 9 if d < 1.4 else (8 if d < 2.4 else 10)
        t[1][8] = 8
    return t


def u_masonry():
    t = blank(13)
    for y in range(16):
        for x in range(16):
            if y % 5 == 4 or (x + (y // 5) * 4) % 8 == 7:
                t[y][x] = 5
            elif hash2(x, y) % 6 == 0:
                t[y][x] = 14
    return t


def u_rails():
    t = blank(5)
    noise(t, [5, 4, 13], 12, 3)
    for y in range(16):
        for x in range(16):
            if y % 4 == 1:
                t[y][x] = 6
    for y in range(16):
        t[y][4] = 14
        t[y][11] = 14
        t[y][5] = 13
        t[y][12] = 13
    return t


def u_pillar():
    t = u_floor(4, (6, 7, 6, 5))
    for y in range(16):
        for x in range(3, 13):
            t[y][x] = 7 if x < 6 else (6 if x < 10 else 5)
            if y in (0, 1, 14, 15):
                t[y][x] = 4 if y in (0, 15) else 7
    return t


def u_shelter():
    t = blank(13)
    noise(t, [13, 5, 14], 33, 5)
    for y in range(16):
        for x in range(16):
            if x % 8 == 0 or y % 8 == 0:
                t[y][x] = 5
    return t


SURFACE_TILES = [
    ("STREET_A", t_basalt(0)), ("STREET_B", t_basalt(1, crack=True)), ("TOMBINO", t_tombino()),
    ("PIAZZA", t_piazza()), ("PEDESTRIAN", t_pedestrian()),
    ("ROOF_A", t_roof(0)), ("ROOF_B", t_roof(1)), ("ROOF_C", t_roof(2)),
    ("FACADE_WINDOW", t_facade(0)), ("FACADE_DOOR", t_facade(1)), ("FACADE_BALCONY", t_facade(2)),
    ("FACADE_SHOP", t_facade(3)), ("PUBLIC_ROOF", t_public_roof()), ("PUBLIC_FACADE", t_public_facade()),
    ("GRASS", t_grass()), ("TREE", t_tree()), ("GALLERIA", t_galleria()), ("SEA", t_sea()),
]
UNDER_TILES = [
    ("U_FLOOR", u_floor(0)), ("U_CISTERN", u_floor(2, (7, 7, 6, 9))), ("U_STAIRS", u_stairs()),
    ("U_WATER", u_water()), ("U_ROCK_A", u_rock(0)), ("U_ROCK_B", u_rock(1)), ("U_ROCKFACE", u_rockface()),
    ("U_MASONRY", u_masonry()), ("U_RAILS", u_rails()), ("U_PILLAR", u_pillar()),
    ("U_TORCH", u_rockface(torch=True)), ("U_SHELTER", u_shelter()),
]

# ---------------------------------------------------------------- sprites
HERO_KEY = {".": 0, "K": 1, "N": 2, "T": 3, "s": 4, "S": 5, "L": 6, "F": 7, "G": 8, "B": 9,
            "n": 10, "l": 11, "d": 12, "W": 13, "Y": 14, "X": 15}
# Entomo, after the only photographs of the costume: navy balaclava with pale
# teal antenna and eye markings, sage long-sleeved top with the black angular
# emblem, black gloves with grey wrist bands, black trousers, brown boots.
HERO_DOWN = """
................
.....KKKKKK.....
....KNNNNNNK....
...KNTNNNNTNK...
...KNNTNNTNNK...
...KNNTNNTNNK...
...KTTTNNTTTK...
...KTWKNNKWTK...
...KNNFFFFNNK...
....KNFFFFNK....
....KKNNNNKK....
...sSSSSSSSSs...
..sSSLKKKKKSSs..
..sSSLLLKKSSSs..
.sSSSLLKKLSSSSs.
.KKSSLKKLLLSSKK.
.KKSSKKKKKLSSKK.
.GG.sSSSSSSs.GG.
.KK.KKKKKKKK.KK.
....KNNKKNNK....
....KNNKKNNK....
....KNK..KNK....
....BBB..BBB....
...XBBBXXBBBX...
"""
HERO_DOWN_LEGS = """
....KNNKKNNK....
....KNNK.KNK....
....KNK..BBB....
....BBB.XXXX....
...XBBBX........
"""
HERO_UP = """
................
.....KKKKKK.....
....KNNNNNNK....
...KNTNNNNTNK...
...KNNTNNTNNK...
...KNNNTTNNNK...
...KNNNNNNNNK...
...KNNNNNNNNK...
...KNNNNNNNNK...
....KNNNNNNK....
....KKNNNNKK....
...sSSSSSSSSs...
..sSSSSSSSSSSs..
..sSSSSSSSSSSs..
.sSSSSSSSSSSSSs.
.KKSSSSSSSSSSKK.
.KKSSSSSSSSSSKK.
.GG.sSSSSSSs.GG.
.KK.KKKKKKKK.KK.
....KNNKKNNK....
....KNNKKNNK....
....KNK..KNK....
....BBB..BBB....
...XBBBXXBBBX...
"""
HERO_SIDE = """
................
......KKKKKK....
.....KNNNNNNK...
....KNNTNNNNK...
....KNNNTNNNK...
....KNNNTNNNK...
....KNNNNTTTK...
....KNNNNKWTK...
....KNNNNFFFK...
.....KNNFFFK....
.....KKNNKK.....
....sSSSSSs.....
...sSSSSKKSs....
...sSSSLLKSs....
...sSKKSKKSs....
...sSKKKLSSs....
...sSSGGKKSs....
....sSSSKKs.....
....KKKKKK......
....KNNKNK......
....KNNKNK......
....KNKKNK......
....BBBBBB......
...XBBBBBBBX....
"""
HERO_SIDE_LEGS = """
....KNNKNNK.....
...KNNK.KNNK....
...KNK...KNK....
..BBB....BBB....
..XBBX..XBBBX...
"""

NPC_KEY = {".": 0, "K": 1, "F": 2, "H": 3, "R": 4, "J": 5, "W": 6, "Y": 7, "G": 8, "B": 9,
           "M": 10, "C": 11, "V": 12, "O": 13, "T": 14, "P": 15}
TEPPISTA = """
................
.....KKKKKK.....
....KRRRRRRK....
...KRRRRRRRRK...
...KRRKKKKRRK...
...KRKFFFFKRK...
...KRKFKFKFRK...
...KRKFFFFKRK...
....KKFFFFKK....
.....KRRRRK.....
...KKRRRRRRKK...
..KRRRRRRRRRRK..
..KRRRWWWWRRRK..
..KRRRRRRRRRRK.C
.KRRKRRRRRRKFKMM
.KRRKRRRRRRKKKMK
.KFFKRRRRRRK.KMK
..KKKJJJJJJK.KKK
....KJJKKJJK....
....KJJKKJJK....
....KJJK.KJJK...
....KJK..KJK....
....WWW..WWW....
...KWWWKKWWWK...
"""
GUARDIA = """
................
.....KKKKKK.....
....KBBBBBBK....
...KBBBBBBBBK...
...KKKKKKKKKKK..
...KFFFFFFFK....
...KFKFFKFFK....
...KFFFFFFFK....
....KFHHHFK.....
.....KFFFK......
...KKYYYYYKK....
..KGYYYYYYYGK...
..KGYWWYWWYGK...
..KGYYYYYYYGK.KK
.KGGKYWWYWYKGKFK
.KGGKYYYYYYKGGFK
.KFFKYYYYYYKKKK.
..KKKJJJJJJK....
....KJJKKJJK....
....KJJKKJJK....
....KJJK.KJJK...
....KJK..KJK....
....BBB..BBB....
...KBBBKKBBBK...
"""
NPC_LEGS = """
....KJJKKJJK....
....KJJK.KJK....
....KJK..BBB....
....BBB.KKKK....
...KBBBK........
"""
READER = """
................
.....KKKKKK.....
....KHHHHHHK....
...KHHHHHHHHK...
...KHFFFFFFHK...
...KFFKFFKFFK...
...KFFFFFFFFK...
....KFFHHFFK....
.....KFFFFK.....
....KKRRRRKK....
...KRRRRRRRRK...
..KRRRRRRRRRRK..
..KRFKWWWWKFRK..
..KRFKWWKWWKFK..
..KKFKWKWWKWFK..
...KKKWWWKWWKK..
....KKKKKKKKK...
....KJJJJJJK....
....KJJKKJJK....
....KJJKKJJK....
....KJJK.KJJK...
....KJK..KJK....
....BBB..BBB....
...KBBBKKBBBK...
"""
GARDENER = """
................
.....KKKKKK.....
....KYYYYYYK....
...KYYYYYYYYK...
..KKKKKKKKKKKK..
...KFFKFFKFFK...
...KFFFFFFFFK...
....KFFHHFFK....
.....KFFFFK.....
....KKVVVVKK....
...KVVVVVVVVK...
..KVVVVVVVVVVK..
..KVVKPVPVKVVK..
..KVFKVVPVKFVK..
..KKFKVPVVKFKK..
...KKKOOOOKKK...
....KOOOOOOK....
....KKOOOOKK....
....KJJKKJJK....
....KJJKKJJK....
....KJJK.KJJK...
....KJK..KJK....
....BBB..BBB....
...KBBBKKBBBK...
"""
OBJECTS = {
    "GRAFFITI_A": """
................
..MM.....CC.....
.MMMM...CCCC..M.
.MM.MM.CC..C.MM.
.MM..MCC...CMM..
.MMMMMCC..MMM...
.MM..MMCCMMCC...
.MM...MMMMCCC.C.
..M..CCMMCC.CCC.
....CC.MM....C..
...CCCMMMMM.....
..CC.MMCC.MM..M.
.....MM.CC.MMMM.
....MM...C..MM..
................
................
""",
    "GRAFFITI_B": """
................
.CC..........M..
.CCC..MMMM..MM..
..CCCMM..MMMM...
...CCM....MM....
..MMCCC..MMMM...
.MM..CCCMM..MM..
.M....CCC....M..
.MM..MMCCC..MM..
..MMMM..CCCMM...
...MM....CCC....
..MMMM..MMCCC...
.MM..MMMM..CCC..
.M....MM....CC..
................
................
""",
    "TRASH": """
................
................
.....KK.........
....KBBK..KK....
...KBBBBKKBBK...
...KBGBBKBBBBK..
..KBBBBBKBGBBK..
..KBBGBBKBBBBK..
.KKBBBBKKKBBBK..
.KOOKBKBBBKBK...
.KOOOKKBGBBKKK..
.KOOOKBBBBBKWK..
..KKKKBBBBBKWWK.
..KTTKKBBBKKKKK.
..KKKK.KKK......
................
""",
    "GLASS": """
................
................
................
.......K........
......KVK.......
......KVK...K...
.....KVVVK.KCK..
.....KVCVK..KCK.
..K..KVVVK...K..
.KCK.KVCVK......
..KCKKVVVK..C...
...K.KKKKK.C.C..
..C..........C..
.C.C..C.C.......
....C.....C.....
................
""",
    "FLOWERS": """
................
................
....P.....P.....
...PYP...PYP....
....P..P..P.....
....V.PYP.V.....
....V..P..V.....
...VVV.V.VVV....
....V..V..V.....
...KKKKKKKKK....
...KOOOOOOOK....
....KOOOOOK.....
....KOOTOOK.....
....KOOOOOK.....
.....KKKKK......
................
""",
    "SPARKLE": """
................
.......W........
.......W........
..W...WWW...W...
...W...W...W....
....W.....W.....
.......W........
.WW...WWW...WW..
.......W........
....W.....W.....
...W...W...W....
..W...WWW...W...
.......W........
.......W........
................
................
""",
}
FG_KEY = {".": 0, "W": 13, "R": 14, "B": 15, "K": 11, "S": 6, "V": 5, "P": 9}
# Foreground layer (1.5x): washing hung across a vicolo, and a stalactite.
LAUNDRY = """
KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK
..K.WWWWWW.K..K.RRRR.K...K.BBBBBBBB.K..K.WWWW.K.
....WWWWWW......RRRR.....BBBBBBBBBB......WWWW...
....WWWWWW.....RRRRRR....BBBBBBBBBB......WWWW...
....WWWWWW.....RRRRRR....BB.BBBB.BB......WWWW...
....WWWWWW.....RR..RR....BB.BBBB.BB......WWWW...
....WWWWWW.....RR..RR.......BBBB.........WWWW...
....WWWWWW.....RR..RR.......BBBB.........W..W...
....WWWWWW..................BBBB................
....WWWWWW..................BBBB................
................................................
................................................
"""
STALACTITE = """
VVVVVVVVVVVVVVVV
SVVVVVVVVVVVVVVS
.SVVVVVVVVVVVVS.
.SVVVSVVVVVVVVS.
..SVVSVVVVVVVS..
..SVVSVVVVSVVS..
...SVVVVVVSVS...
...SVVVVVVSVS...
...SVVSVVVVS....
....SVSVVVVS....
....SVSVVVS.....
....SVVVVVS.....
.....SVVVS......
.....SVSVS......
.....SVVS.......
......SVS.......
......SVS.......
......SS........
......SS........
.......S........
.......S........
................
................
................
"""


def parse(art, key, w, h):
    rows = [r for r in art.strip("\n").split("\n")]
    assert len(rows) == h and all(len(r) == w for r in rows), (len(rows), [len(r) for r in rows], art[:40])
    return [[key[c] for c in r] for r in rows]


def with_legs(base, legs, key):
    rows = base.strip("\n").split("\n")
    lrows = legs.strip("\n").split("\n")
    return "\n".join(rows[:len(rows) - len(lrows)] + lrows)


def portrait():
    """48x64 bust in the crossed-arms pose of the photographs (palette 2)."""
    from PIL import Image, ImageDraw
    S = 4
    im = Image.new("P", (48 * S, 64 * S), 0)
    d = ImageDraw.Draw(im)
    q = lambda pts: [(x * S, y * S) for x, y in pts]
    d.polygon(q([(4, 64), (6, 34), (14, 28), (34, 28), (42, 34), (44, 64)]), fill=5)       # torso
    d.polygon(q([(6, 34), (14, 28), (18, 28), (10, 64), (4, 64)]), fill=4)                 # shade
    d.polygon(q([(30, 28), (34, 28), (42, 34), (44, 64), (40, 64)]), fill=6)               # light
    d.polygon(q([(18, 22), (30, 22), (33, 30), (15, 30)]), fill=1)                         # neck
    d.ellipse([13 * S, 2 * S, 35 * S, 27 * S], fill=2)                                    # hood
    d.ellipse([13 * S, 2 * S, 22 * S, 27 * S], fill=1)
    d.ellipse([15 * S, 2 * S, 35 * S, 27 * S], fill=2)
    d.polygon(q([(18, 19), (30, 19), (29, 26), (24, 28), (19, 26)]), fill=7)               # jaw
    d.polygon(q([(15, 13), (24, 15), (33, 13), (33, 19), (24, 21), (15, 19)]), fill=1)     # eye band
    for sx in (-1, 1):                                                                     # markings
        cx = 24 + sx * 4.5
        d.ellipse([(cx - 3.2) * S, 14.2 * S, (cx + 3.2) * S, 19 * S], outline=3, width=S)
        d.ellipse([(cx - 1.4) * S, 15.6 * S, (cx + 1.4) * S, 17.8 * S], fill=7)
        d.line(q([(cx, 14), (24 + sx * 3, 9), (24 + sx * 5, 5), (24 + sx * 7.5, 3.5)]), fill=3, width=S)
    d.polygon(q([(15, 60), (30, 60), (22, 64), (12, 64)]), fill=1)                         # emblem
    d.polygon(q([(22, 54), (34, 54), (30, 60), (26, 60)]), fill=1)
    # crossed forearms: black sleeves, grey wrist bands, black fists
    d.polygon(q([(5, 50), (12, 58), (38, 36), (32, 28)]), fill=1)
    d.polygon(q([(43, 50), (36, 58), (10, 36), (16, 28)]), fill=2)
    d.polygon(q([(26, 33), (31, 38), (36, 34), (31, 29)]), fill=8)
    d.polygon(q([(22, 33), (17, 38), (12, 34), (17, 29)]), fill=8)
    d.ellipse([29 * S, 24 * S, 39 * S, 34 * S], fill=1)
    d.ellipse([9 * S, 24 * S, 19 * S, 34 * S], fill=2)
    d.line(q([(10, 36), (36, 58)]), fill=1, width=S)
    small = im.resize((48, 64), Image.NEAREST)
    px = list(small.getdata())
    return [px[y * 48:(y + 1) * 48] for y in range(64)]


# ----------------------------------------------------------------- tables
S_CODE = {".": 0, ":": 1, "O": 2, "+": 3, "#": 4, "P": 5, "g": 6, "T": 7, "~": 8, "G": 9}
U_CODE = {".": 0, ",": 1, "O": 2, "w": 3, "#": 4, "M": 5, "=": 6, "I": 7, "~": 8, "t": 9, "s": 10}
S_WALK, U_WALK = ".:O+gG", ".,Ow=s"
TASK_GRAFFITI, TASK_TRASH, TASK_GLASS = 0, 1, 2
PER_ZONE = {"surface": {TASK_GRAFFITI: 6, TASK_TRASH: 5, TASK_GLASS: 2}, "under": {TASK_GRAFFITI: 1, TASK_TRASH: 2}}
ENEMIES_PER_ZONE = (3, 2)          # teppistelli, guardiamacchine


def load(name):
    return [list(r) for r in (ROOT / "assets/maps" / name).read_text(encoding="ascii").splitlines()]


def zone_of(x, y):
    for i, (_n, x0, y0, x1, y1) in enumerate(ZONES):
        if x0 <= x < x1 and y0 <= y < y1:
            return i
    raise ValueError((x, y))


def place(surface, under, start):
    """Deterministic task and actor placement, spread by a coordinate hash."""
    tasks, taken = [], [start]
    far = lambda x, y, d: all(abs(x - tx) + abs(y - ty) >= d for tx, ty in taken)

    def candidates(grid, walk, kind, layer):
        out = []
        for y in range(2, N - 2):
            for x in range(2, N - 2):
                t = grid[y][x]
                if t not in walk or t in "OGgw=":
                    continue
                above = grid[y - 1][x]
                if kind == TASK_GRAFFITI and above not in ("#P" if layer == 0 else "#M"):
                    continue
                if kind != TASK_GRAFFITI and layer == 1 and t not in ",s":
                    continue
                out.append((hash2(x * 7 + kind * 131 + layer * 977, y * 13 + kind), x, y))
        return [(x, y) for _h, x, y in sorted(out)]

    for layer, grid, walk, name in ((0, surface, S_WALK, "surface"), (1, under, U_WALK, "under")):
        for kind, per_zone in PER_ZONE[name].items():
            count = [0] * len(ZONES)
            for spacing in (9, 6, 4):
                for x, y in candidates(grid, walk, kind, layer):
                    z = zone_of(x, y)
                    if count[z] < per_zone and far(x, y, spacing):
                        tasks.append((x, y, layer, kind))
                        taken.append((x, y))
                        count[z] += 1
            assert min(count) == per_zone, (name, kind, count)
    tasks.sort(key=lambda t: (zone_of(t[0], t[1]), t[2], t[1], t[0]))

    spawns = []
    for kind, per_zone in enumerate(ENEMIES_PER_ZONE):
        count = [0] * len(ZONES)
        cand = sorted((hash2(x * 3 + kind * 71, y * 5 + 9), x, y) for y in range(3, N - 3) for x in range(3, N - 3)
                      if surface[y][x] in ".+:")
        for _h, x, y in cand:
            z = zone_of(x, y)
            if count[z] < per_zone and all(abs(x - sx) + abs(y - sy) >= 14 for sx, sy, _k in spawns) \
                    and abs(x - start[0]) + abs(y - start[1]) >= 12:
                spawns.append((x, y, kind))
                count[z] += 1
        assert min(count) == per_zone, count
    return tasks, spawns


def pack4(rows):
    out = []
    for r in rows:
        assert len(r) % 2 == 0
        for i in range(0, len(r), 2):
            assert 0 <= r[i] < 16 and 0 <= r[i + 1] < 16
            out.append((r[i] << 4) | r[i + 1])
    return out


def c_bytes(name, data, per_line=24, ctype="uint8_t", dims=None):
    """C table; a two-dimensional `dims` such as "[6][192]" gets one braced row per entry."""
    dim = dims or f"[{len(data)}]"
    lines = [f"static const {ctype} {name}{dim} = {{"]
    if dims:
        row = int(dims.rsplit("[", 1)[1].rstrip("]"))
        assert len(data) % row == 0
        for i in range(0, len(data), row):
            lines.append("    {" + ",".join(str(v) for v in data[i:i + row]) + "},")
    else:
        for i in range(0, len(data), per_line):
            lines.append("    " + ",".join(str(v) for v in data[i:i + per_line]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sheet", type=Path)
    args = ap.parse_args()
    surface, under = load("surface.map"), load("underground.map")
    assert len(surface) == N and len(under) == N

    # Palette. Hosts with an RGB565 surface draw PAL_RGB directly. On the
    # indexed ILI9341 framebuffer the firmware quantises every colour to a
    # cell of its 6x6x6 system cube, so the cartridge gives each of its
    # colours a private cell: it draws with PAL_PROXY (a colour that
    # quantises to that cell) and redefines the cell with the true colour
    # through prg32_palette_set. PAL_CELL lists the cells.
    flat = [rgb565(c) for pal in PALETTES for c in pal]
    cells, proxies = [], []
    for cell in range(17, 232):
        r, g, b = (cell - 16) // 36, (cell - 16) // 6 % 6, (cell - 16) % 6
        proxy = (-(-r * 31 // 5) << 11) | (-(-g * 63 // 5) << 5) | -(-b * 31 // 5)
        if cube_cell(proxy) == cell and len(cells) < len(flat):
            cells.append(cell)
            proxies.append(proxy)
    assert len(cells) == len(flat) == len(set(cells))

    shafts = {}
    for key, _name, x, y, _kind, _net in NODES:
        best = min(((xx - x) ** 2 + (yy - y) ** 2, xx, yy) for yy in range(N) for xx in range(N)
                   if surface[yy][xx] == "O")
        shafts[key] = (best[1], best[2])
    gx, gy = shafts["galleria"]
    start = next((gx + dx, gy + dy) for dx, dy in ((0, 1), (1, 0), (-1, 0), (0, -1))
                 if surface[gy + dy][gx + dx] in S_WALK)
    tasks, spawns = place(surface, under, start)

    def pack_map(grid, code):
        out = []
        for row in grid:
            for x in range(0, N, 2):
                out.append(code[row[x]] | (code[row[x + 1]] << 4))
        return out

    hero = [parse(HERO_DOWN, HERO_KEY, 16, 24), parse(with_legs(HERO_DOWN, HERO_DOWN_LEGS, HERO_KEY), HERO_KEY, 16, 24),
            parse(HERO_UP, HERO_KEY, 16, 24), parse(with_legs(HERO_UP, HERO_DOWN_LEGS, HERO_KEY), HERO_KEY, 16, 24),
            parse(HERO_SIDE, HERO_KEY, 16, 24), parse(with_legs(HERO_SIDE, HERO_SIDE_LEGS, HERO_KEY), HERO_KEY, 16, 24)]
    npcs = [parse(TEPPISTA, NPC_KEY, 16, 24), parse(with_legs(TEPPISTA, NPC_LEGS, NPC_KEY), NPC_KEY, 16, 24),
            parse(GUARDIA, NPC_KEY, 16, 24), parse(with_legs(GUARDIA, NPC_LEGS, NPC_KEY), NPC_KEY, 16, 24),
            parse(READER, NPC_KEY, 16, 24), parse(GARDENER, NPC_KEY, 16, 24)]
    objs = [(n, parse(a, NPC_KEY, 16, 16)) for n, a in OBJECTS.items()]
    laundry = parse(LAUNDRY, FG_KEY, 48, 12)
    stalactite = parse(STALACTITE, FG_KEY, 16, 24)
    bust = portrait()

    # The first NODE_COUNT places are the shafts, at their real (snapped) tiles.
    gazetteer = [(name, *shafts[key]) for key, name, *_r in NODES] + list(GAZETTEER[len(NODES):])
    names = []
    for name, _x, _y in gazetteer:
        if name not in names:
            names.append(name)
    assert all(len(n) <= 23 and all(c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ?!.,:-+/" for c in n) for n in names), names

    out = ["/* Generated by tools/build_assets.py - do not edit. */", "#ifndef ENTOMO_GEN_ASSETS_H",
           "#define ENTOMO_GEN_ASSETS_H", "", f"#define MAP_N {N}",
           f"#define START_TX {start[0]}", f"#define START_TY {start[1]}",
           f"#define PAL_COUNT {len(flat)}", ""]
    for i, (name, _t) in enumerate(SURFACE_TILES + UNDER_TILES):
        out.append(f"#define GFX_{name} {i}")
    out.append(f"#define GFX_COUNT {len(SURFACE_TILES) + len(UNDER_TILES)}")
    for i, (name, _a) in enumerate(objs):
        out.append(f"#define OBJ_{name} {i}")
    out += [f"#define TASK_COUNT {len(tasks)}", f"#define SPAWN_COUNT {len(spawns)}",
            f"#define PLACE_COUNT {len(GAZETTEER)}", f"#define NAME_COUNT {len(names)}",
            f"#define ZONE_COUNT {len(ZONES)}", f"#define NODE_COUNT {len(NODES)}", ""]
    out.append(c_bytes("PAL_RGB", [f"0x{c:04x}" for c in flat], 8, "uint16_t"))
    out.append(c_bytes("PAL_PROXY", [f"0x{c:04x}" for c in proxies], 8, "uint16_t"))
    out.append(c_bytes("PAL_CELL", cells, 16))
    out.append(c_bytes("napoli_surface_map", pack_map(surface, S_CODE), 32))
    out.append(c_bytes("napoli_subterranean_map", pack_map(under, U_CODE), 32))
    out.append(c_bytes("TILE_GFX", [b for _n, t in SURFACE_TILES + UNDER_TILES for b in pack4(t)], 32,
                       dims=f"[{len(SURFACE_TILES) + len(UNDER_TILES)}][128]"))
    out.append(c_bytes("SPR_HERO", [b for s in hero for b in pack4(s)], 32, dims=f"[{len(hero)}][192]"))
    out.append(c_bytes("SPR_NPC", [b for s in npcs for b in pack4(s)], 32, dims=f"[{len(npcs)}][192]"))
    out.append(c_bytes("SPR_OBJ", [b for _n, s in objs for b in pack4(s)], 32, dims=f"[{len(objs)}][128]"))
    out.append(c_bytes("SPR_LAUNDRY", pack4(laundry), 24))
    out.append(c_bytes("SPR_STALACTITE", pack4(stalactite), 32))
    out.append(c_bytes("SPR_BUST", pack4(bust), 24))
    out.append("/* x, y, layer | kind << 1 */")
    out.append(c_bytes("TASKS", [v for x, y, layer, kind in tasks for v in (x, y, layer | (kind << 1))], 24,
                       dims=f"[{len(tasks)}][3]"))
    out.append("/* x, y, kind (0 teppistello, 1 guardiamacchine) */")
    out.append(c_bytes("SPAWNS", [v for s in spawns for v in s], 24, dims=f"[{len(spawns)}][3]"))
    out.append("/* x, y, name index */")
    out.append(c_bytes("PLACES", [v for name, x, y in gazetteer for v in (x, y, names.index(name))], 24,
                       dims=f"[{len(GAZETTEER)}][3]"))
    out.append(f"static const char PLACE_NAMES[{len(names)}][24] = {{")
    out += [f'    "{n}",' for n in names] + ["};"]
    out.append("/* x0, y0, x1, y1 (half-open, tiles) */")
    out.append(c_bytes("ZONES", [v for _n, *r in ZONES for v in r], 4, dims=f"[{len(ZONES)}][4]"))
    out.append(f"static const char ZONE_NAMES[{len(ZONES)}][16] = {{")
    out += [f'    "{n}",' for n, *_r in ZONES] + ["};"]
    out += ["", "#endif", ""]
    target = ROOT / "src/gen_assets.h"
    target.write_text("\n".join(out), encoding="ascii")
    per_zone = [sum(1 for t in tasks if zone_of(t[0], t[1]) == z) for z in range(len(ZONES))]
    print(f"wrote {target}: {len(tasks)} tasks {per_zone}, {len(spawns)} actors, {len(GAZETTEER)} places, "
          f"{len(flat)} colours, start {start}")

    if args.sheet:
        from PIL import Image
        rgb = [c for pal in PALETTES for c in pal]
        sheet = Image.new("RGB", (16 * 20, 16 * 12), (40, 40, 40))
        px = sheet.load()

        def blit(rows, pal, ox, oy):
            for y, r in enumerate(rows):
                for x, v in enumerate(r):
                    if v or pal < 2:
                        px[ox + x, oy + y] = rgb[pal * 16 + v]
        for i, (_n, t) in enumerate(SURFACE_TILES):
            blit(t, 0, (i % 20) * 16, 0)
        for i, (_n, t) in enumerate(UNDER_TILES):
            blit(t, 1, (i % 20) * 16, 20)
            blit(t, 5, (i % 20) * 16, 38)
        for i, s in enumerate(hero):
            blit(s, 2, i * 18, 60)
        for i, s in enumerate(npcs):
            blit(s, 3, 120 + i * 18, 60)
        for i, (_n, s) in enumerate(objs):
            blit(s, 3, i * 18, 90)
        blit(laundry, 4, 120, 90)
        blit(stalactite, 4, 180, 90)
        blit(bust, 2, 0, 112)
        args.sheet.parent.mkdir(parents=True, exist_ok=True)
        sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(args.sheet)
    return 0


if __name__ == "__main__":
    sys.exit(main())
