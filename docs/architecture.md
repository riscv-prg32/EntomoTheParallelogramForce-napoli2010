# Architecture

One C file, `src/entomo.c` (about 1,400 lines), plus the generated tables
in `src/gen_assets.h`. No libc, no floats, no pointers in initialised data.

## Frame

The PRG32 firmware calls `entomo_update()` and `entomo_draw()` once per
33 ms frame. The design brief asked for 60 frames per second; the firmware
loop is fixed at 30, so every speed and timer here is in 1/30 s frames.

## Renderer

PRG32 offers 8x8 two-colour tile playfields (64x32 tiles). This game needs
16-colour 16x16 tiles, a 128x128 map and three parallax layers, so it
composes the picture itself and uses the firmware as a blitter.

The 304x176 playfield is drawn as eleven strips of sixteen scanlines. Each
strip is composed in a 5 KiB buffer and handed over with one
`prg32_sprite_draw_indexed()` call as an 8-bit sprite with a 96-entry
palette. For every strip, back to front:

| Layer | Speed | Content |
| --- | --- | --- |
| Backdrop | 0.25x | procedural: Vesuvius across the gulf at dusk, or the pillars of a quarry. Drawn only where the map has an "open" tile (sea, pit) |
| Playfield | 1.0x | map tiles (4 bpp, decoded on the fly), jobs, actors, hero, shield |
| Foreground | 1.5x | washing lines or stalactites, twice the size, over everything |

All collision is on the playfield plane, tile based, with an 11x8 pixel box
at the feet.

The oblique view needs no extra map data: a block tile with a street to its
south is drawn as a facade (window, door, balcony or shop, chosen by a
coordinate hash), every other block tile as a roof; under ground a rock
tile above a floor is drawn as a tool-marked wall.

Below ground a 21x13 light grid is computed each frame from the hero's
lamp and the torch tiles: lit tiles use the palette, dim ones a darkened
copy, the rest is black.

The ledger frame around the playfield is drawn once; the HUD text inside it
is refreshed every fourth frame.

## Colour on three kinds of display

The cartridge uses 96 colours (six palettes of 16) and wants exactly those
on every PRG32 host:

| Display | How colours get there |
| --- | --- |
| RGB565 surface (QEMU firmware) | drawn as they are |
| ILI9341 indexed framebuffer (the board) | the firmware quantises a colour by formula to a cell of its 6x6x6 cube; the game draws with a *proxy* colour that lands in a private cell and redefines that cell with the true colour (`prg32_palette_set`) |
| Indexed with nearest-entry lookup (PRG32-QT) | the same cells are redefined; drawing the true colour finds them |

`detect_display()` tells them apart at start-up with two pixels and one
`prg32_gfx_snapshot_row_rgb565()`. The host tests run the cartridge on a
model of each and require identical pixels. Colour cycling (shield neon,
torch flicker, water) is three palette writes every third frame.

## Maps and the layer switch

`napoli_surface_map` and `napoli_subterranean_map` are `const` tables,
nibble-packed (4 bits per tile, 8 KiB each). `map_cur` points to the
active one. SELECT on a tile of type 2 (manhole above, staircase below)
swaps the pointer; the world coordinates do not change. The hero is first
centred on the shaft tile, because the walls around its two ends differ.

A note on "Flash": the brief asks for the maps in Flash to spare RAM. On
PRG32 a cartridge image is copied whole into executable RAM, so `const`
data costs RAM too; packing the maps to 16 KiB is what leaves room for
everything else.

## The non-violent physics

```c
a->vx = (int16_t)(-a->vx * 2);
a->vy = (int16_t)(-a->vy * 2);
```

is all that happens to an actor that touches the shield: it slides back
with that velocity for 18 frames or until a wall, re-centres on its tile
and walks on. No structure in the program has a health field.

## Memory

| Part | Bytes |
| --- | ---: |
| Two maps | 16,384 |
| Tiles, sprites, bust | about 8,900 |
| Places, jobs, palettes, names | about 2,200 |
| Code | about 17,200 |
| **Code and data** | **44,656** |
| bss (strip buffer, palette, state) | 6,316 |
| **Executable RAM** | **50,972 of 65,536** |
| AUDIO block | 3,212 |
| Store trailer (metadata, colophon, icon, screenshot) | about 12,000 |
| **Package** | **60,049 of 65,536** |

## Where the cartridge departs from the brief

| Brief | Cartridge | Why |
| --- | --- | --- |
| 60 FPS | 30 | the firmware frame is 33 ms |
| SELECT and START as two buttons | one contextual button | they are the same input on PRG32 |
| Maps in Flash | `const`, nibble-packed, in cartridge RAM | cartridges run from RAM |
| Side-view vertical scrolling (scaffolding, shafts) | oblique top view, scrolling in every direction, with a second map beneath | the street atlas is a plan; "vertical" became the layer under the streets |
| Spaccanapoli | its lower cut only | the upper cut is on plates of the next row |
