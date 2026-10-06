# Gameplay

## Goal

Bring the Decoro Index of the four rioni to 100 before dawn.

| Rione | Area of the window | Jobs |
| --- | --- | ---: |
| Montecalvario | west of Via Toledo, Tav. 35 | 16 |
| San Giuseppe | east of Via Toledo, Tav. 36 | 16 |
| San Ferdinando | west of Via Toledo, Tav. 37 | 16 |
| Porto | Galleria, Municipio, the port, Tav. 38 | 16 |

Via Toledo and the boundary between the upper and the lower plates split
the window; the names are those of the real quartieri that meet there.
In each rione thirteen jobs are in the streets and three are under ground.

| Job | Where | Per rione (above + below) | Hold A for | Score |
| --- | --- | ---: | ---: | ---: |
| Graffiti | on the wall above a street or gallery tile | 6 + 1 | 36 frames | 100 |
| Rubbish | on the street, or in a cistern | 5 + 2 | 20 frames | 60 |
| Broken glass | on the street | 2 + 0 | 28 frames | 80 |

The counts are `PER_ZONE` in `tools/build_assets.py`. Graffiti fade as they are scrubbed; a cleared heap of rubbish
leaves a flower pot. Every finished job also returns some FORZA.

## The night

The clock runs from 22:00 to 06:00: 480 game minutes, 56 frames each, that
is 15 minutes of play at the firmware's 30 frames per second. The patrol
starts in the Galleria Umberto I, one tile south of a manhole. At dawn, or
when the fourth rione is done, the ending shows the four indexes and the
score; A saves it in the PRG32 score table (`entomo`).

Score: the jobs, 25 per bounce, 1000 per completed rione, and 10 per
minute left when all four are done.

## The Parallelogram Force

Hold B. A neon wireframe box, sheared along the 2:1 isometric diagonal,
surrounds the hero. It drains FORZA (about six seconds from full), needs a
small reserve to start, and recharges when released. The hero walks a
little slower with it and cannot clean while it is up.

A troublemaker that touches it bounces: its velocity is inverted and
doubled, it slides back until a wall stops it, then walks on. That is the
whole effect.

## Troublemakers

| | Speed | Behaviour |
| --- | --- | --- |
| Teppistello (3 per rione) | 1.25 px/frame | wanders the streets, turning at random |
| Guardiamacchine (2 per rione) | 0.75 px/frame | wanders, but walks up to the hero within 7 tiles |

They live on the surface only. A contact without the shield makes the
clock jump eight minutes ahead ("time lost"), then the hero is left alone
for two and a half seconds. Nothing else happens to anybody.

When a rione reaches 100 every troublemaker that belongs to it stops for
good: the teppistello reads a book, the guardiamacchine tends a flower pot.
They can no longer be touched or bounced.

## The non-violence guardrail

The design brief rejects punitive or sensationalist "citizen shaming"
dynamics, and so does the code: there is no attack, no health variable, no
arrest, no score for confronting anyone beyond the small bounce bonus, and
the game never names or blames the people who made the mess. The only
verbs are walk, clean, shield, descend.

## The atlas (START)

A page of the street atlas: the 128x128 map at 1.5 pixels per tile with the
magenta atlas squares, the jobs still to do in red (blinking), the shafts in
blue, the hero in green; beside it the four indexes. Below ground it shows
the galleries instead. START or B closes it.

## Under ground

Only the hero's lamp and the wall torches give light: tiles are lit, dim or
black. Pits and the open sea are "open" tiles through which the slow
backdrop shows. There are no troublemakers below, only water dripping in
the dark.
