"""Places shared by the map tools: the vertical links between the two layers.

Coordinates are game tiles (0..127) inside the atlas window described in
tools/build_map.py; they were read from the positions of the street-name
labels of the Tuttocitta' plates. Each node is a manhole (tombino) or an
hypogeum staircase on the surface and the foot of the same shaft below.

`kind` selects what build_underground.py digs at the foot of the shaft.
The descriptions summarise the public sources listed in docs/maps.md; the
exact course of the galleries between the nodes is not public and is
generated (it follows the streets above, as the aqueduct channels did).
"""

# (key, name shown in game, x, y, kind, network)
NODES = [
    ("scura",      "VIA PASQUALE SCURA",   47,  31, "well",     "carmignano"),
    ("carita",     "PIAZZA CARITA",        58,  24, "cistern",  "carmignano"),
    ("montecalv",  "MONTECALVARIO",        34,  45, "quarry",   "carmignano"),
    ("toledo",     "CANTIERE METRO TOLEDO", 56, 37, "metro",    "metro"),
    ("trinita",    "TRINITA DEGLI SPAGNOLI", 40, 82, "cistern", "carmignano"),
    ("concordia",  "PIAZZETTA CONCORDIA",  14,  87, "cistern",  "carmignano"),
    ("corsove",    "CORSO VITTORIO EMANUELE", 8, 70, "funicular", "funicular"),
    ("augusteo",   "FUNICOLARE AUGUSTEO",  52,  96, "funicular", "funicular"),
    ("santanna",   "S. ANNA DI PALAZZO",   30, 113, "shelter",  "carmignano"),
    ("trieste",    "TRIESTE E TRENTO",     58, 117, "bourbon",  "carmignano"),
    ("galleria",   "GALLERIA UMBERTO I",   64, 100, "salone",   "bolla"),
    ("municipio",  "CANTIERE MUNICIPIO", 76, 77, "port",  "metro"),
    ("maschio",    "MASCHIO ANGIOINO",     97, 101, "dungeon",  "bolla"),
    ("medina",     "VIA MEDINA",           91,  53, "cistern",  "bolla"),
    ("catalana",   "RUA CATALANA",        107,  46, "well",     "bolla"),
    ("monteoliv",  "MONTEOLIVETO",         70,  10, "crypt",    "bolla"),
]

# Wells without a manhole: blind cisterns reached only from below. The
# Carmignano alone fed thousands of them; these stand for the many cavities
# the 2010 municipal census recorded under the two districts. (x, y, network)
WELLS = [
    (22, 22, "carmignano"), (18, 40, "carmignano"), (26, 60, "carmignano"), (44, 62, "carmignano"),
    (20, 100, "carmignano"), (42, 104, "carmignano"), (46, 118, "carmignano"), (12, 112, "carmignano"),
    (84, 20, "bolla"), (100, 26, "bolla"), (112, 72, "bolla"), (84, 96, "bolla"), (70, 58, "bolla"),
    (100, 82, "bolla"), (118, 20, "bolla"),
]

# Place names for the "you are here" line of the HUD: (name, x, y).
GAZETTEER = [(name, x, y) for _, name, x, y, _, _ in NODES] + [
    ("VIA TOLEDO", 60, 12), ("VIA TOLEDO", 55, 53), ("VIA TOLEDO", 54, 70), ("VIA TOLEDO", 54, 104),
    ("VIA SPERANZELLA", 46, 59), ("VIA SPERANZELLA", 45, 78), ("VIA SPERANZELLA", 44, 95),
    ("VICO LUNGO GELSO", 49, 58), ("VICO TRE REGINE", 39, 51), ("VIA MONTECALVARIO", 42, 47),
    ("VICO DUE PORTE", 47, 50), ("VIA EMANUELE DE DEO", 24, 62), ("VIA PORTACARRESE", 20, 56),
    ("VICO TRE RE A TOLEDO", 39, 74), ("VICO GIARDINETTO", 28, 67), ("SALITA CONCORDIA", 21, 82),
    ("VIA NARDONES", 30, 121), ("VIA SUOR ORSOLA", 5, 82), ("VICO DEL CONSIGLIO", 41, 25),
    ("VIA SIMONELLI", 45, 26), ("S. MARIA OGNIBENE", 21, 25), ("VIA NUOVA S. MARIA", 18, 43),
    ("PIAZZA MATTEOTTI", 82, 34), ("VIA DIAZ", 70, 37), ("VIA SANFELICE", 108, 33),
    ("VIA MEDINA", 91, 62), ("VIA S. TOMMASO D AQUINO", 68, 54), ("PALAZZO S. GIACOMO", 62, 73),
    ("PIAZZA MUNICIPIO", 80, 82), ("VIA S. BRIGIDA", 66, 91), ("VIA VERDI", 73, 93),
    ("TEATRO S. CARLO", 63, 111), ("PALAZZO REALE", 72, 122), ("VIA ACTON", 100, 116),
    ("UNIV. PARTHENOPE", 105, 110), ("MOLO BEVERELLO", 118, 114), ("LARGO ECCE HOMO", 104, 12),
    ("VIA DEPRETIS", 112, 62), ("VIA CHIAIA", 44, 122), ("VICO BERIO", 49, 102),
]

# The four rioni of the Decoro Index: (name, x0, y0, x1, y1), half-open.
# Via Toledo and the Tav.35-36 / Tav.37-38 plate boundary split the window.
ZONES = [
    ("MONTECALVARIO", 0, 0, 58, 64),
    ("SAN GIUSEPPE", 58, 0, 128, 64),
    ("SAN FERDINANDO", 0, 64, 56, 128),
    ("PORTO", 56, 64, 128, 128),
]
