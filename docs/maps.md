# The two maps

Both maps are 128x128 tiles of 16x16 pixels (a 2048x2048 pixel world) and
share one coordinate system: tile (x, y) of the subterranean map is the
ground under tile (x, y) of the surface map.

## Surface: from the street atlas

Source: *Tuttocitta Napoli* (SEAT Pagine Gialle), the street atlas
supplied with the design brief, PDF pages 42-45, that is plates
**Tav. 35, 36, 37 and 38**. The atlas is copyrighted: neither the PDF nor
any render of it is in this repository. What is committed is
`assets/maps/surface.map`, a grid of 16,384 characters derived from it.

Why these plates: they share one scale and abut exactly, each a 4x5 grid
of squares. The neighbouring plates (26 and 39) are drawn at another scale
and would not join. The window is 8x8 squares:

```text
            Tav.35 / Tav.37        Tav.36 / Tav.38
columns     A  B  C  D             A  B  C  D
rows        Tav.35-36 rows 2..5,   Tav.37-38 rows 1..4
```

One atlas square is 16x16 tiles, about 9.4 m per tile, so the grid
reference in the HUD is the printed one. It covers the Quartieri Spagnoli
from Corso Vittorio Emanuele to Via Toledo, the lower cut of Spaccanapoli
(Via Pasquale Scura), Piazza Carita, Monteoliveto, Via Medina, Piazza
Municipio, the Galleria Umberto I, the San Carlo, the Maschio Angioino and
the Beverello pier. The upper cut of Spaccanapoli (from Piazza del Gesu
on) is on Tav. 32-33, north of the window.

Method (`tools/build_map.py`, deterministic, about two seconds):

1. render the four pages without text (`gs -dFILTERTEXT`), crop the map
   frames by their grid lines and stitch them;
2. classify every pixel by the flat cartographic colour: street,
   pedestrian area, block, building of public interest, green, water;
3. thin the street mask to centre lines (Zhang-Suen), so that a vicolo
   narrower than a tile still becomes a one-tile path;
4. decide each tile from the centre lines and the class fractions; repair
   joints that touch only by a corner; close the border; keep the largest
   connected street network.

Tile types: street, pedestrian area, piazza or wide carriageway, block,
building of public interest, grass, trees, sea, galleria floor, manhole.
5,374 tiles are walkable and all of them are connected.

Place names and their positions (the "you are here" line, the shafts) were
read from the coordinates of the street-name labels in the same pages; see
`tools/nodes.py`.

```bash
tools/build_map.py /path/to/TUT_NAPOLI.pdf --preview build/surface.png
tools/build_underground.py --preview build/underground.png
tools/build_assets.py --sheet build/sheet.png
```

What the conversion loses: a street narrower than about 9 m is one tile
wide whatever its real width; two parallel vicoli closer than two tiles can
merge; diagonal streets become staircases. The topology of the network is
kept, the metric detail is not.

## Subterranean: documented places, generated galleries

There is no public survey of the Naples subsoil at street scale, so this
layer is a reconstruction (`tools/build_underground.py`). It is honest
about which is which.

**Documented** (public sources below):

| Shaft | What is there |
| --- | --- |
| S. Anna di Palazzo | the Carmignano aqueduct cisterns and the air-raid shelter visited from Vico S. Anna di Palazzo 52, about 40 m down |
| Trieste e Trento | a stub of the Bourbon tunnel, which runs south under Pizzofalcone (off the map) |
| Cantiere Metro Toledo, Cantiere Municipio | Linea 1 building sites in 2010; at Municipio the dig that uncovered the Roman harbour and its ships |
| Funicolare Augusteo, Corso Vittorio Emanuele | the inclined tunnel of the Funicolare Centrale |
| Galleria Umberto I | the Salone Margherita, the hall in the basement of the Galleria |
| Maschio Angioino | the pit under the castle |
| Monteoliveto | the crypt of the church of Sant'Anna dei Lombardi |
| Montecalvario, Trinita degli Spagnoli, Concordia, Carita, Medina, Scura, Rua Catalana | tuff quarries and cisterns of the kind recorded all over these districts |

**Generated:**

- the course of every gallery. Aqueduct galleries join the shafts along the
  shortest path *through the streets above*, because the channels fed the
  wells street by street: under ground you walk under real streets. The
  network west of Via Toledo stands for the Carmignano (1629), the one
  east of it for the older Bolla;
- fifteen blind cisterns with no manhole, and the small ones along the
  galleries: they stand for the roughly 900 cavities that the municipal
  census counted in 2010;
- the size and shape of every room, the straight course of the metro
  tunnel between its two shafts, torches, pillars and pits.

Sources:

- [La Napoli che c'e sotto Napoli](https://www.italytraveller.com/it/e/citta-sotterranee) (Carmignano aqueduct, cisterns, shelters, Vico S. Anna di Palazzo)
- [Galleria Borbonica](https://www.napolike.it/turismo/place/tunnel-borbonico-napoli) and [siviaggia.it](https://siviaggia.it/posti-incredibili/galleria-borbonica-napoli/575186/) (entrances, depth, cisterns, wartime use)
- [Perche le strade di Napoli continuano a collassare](https://www.ilpost.it/2024/03/06/perche-napoli-strade-voragini/) and the [ISPRA database of the cavities of Naples](https://www.isprambiente.gov.it/it/attivita/suolo-e-territorio/sinkholes-e-cavita-sotterranee/banca-dati-nazionale-delle-cavita-sotterranee-e-dei-sinkhole-nei-centri-urbani-italiani/banca-dati-delle-cavita-sotterranee-e-dei-sinkhole-della-citta-di-napoli) (the 2010 census: about 900 cavities, 60 hectares)
- [Naples metro, Linea 1](https://www.urbanrail.net/eu/it/nap/napoli.htm) and [Municipio station](https://audiala.com/it/italia/napoli/municipio) (the line, the Roman harbour dig)
- [List of real-life superheroes](https://en.wikipedia.org/wiki/List_of_real-life_superheroes) (Entomo)

## Checks

`tools/build_underground.py` verifies that the two layers together are one
connected world and turns any unreachable pocket into rock.
`tests/run_tests.sh` verifies that the shafts coincide, that every job and
every actor stands on a walkable tile, and a bot then reaches and cleans
all 64 jobs through the controller.
