# Audio

No samples. Fourteen SID-like instruments and five tracker tunes, written
as text in `tools/build_audio.py` and packed into a 3.2 KB AUDIO block.

## Stereo plan

The PRG32 tracker plays instrument *n* on channel *n*, so the music owns
channels 0-3 and its instruments carry their place on the stage:

| Channel | Voice | Oscillator | Pan |
| ---: | --- | --- | --- |
| 0 | bass | pulse, low-pass | centre |
| 1 | lead (mandolin-like pluck) | saw | left |
| 2 | bells / arpeggio | triangle | right |
| 3 | tammorra and tambourine | noise | far right |
| 4 | shield hum (sustained while B is held) | saw, resonant | centre |
| 5 | effects | instruments 8-13 | where it happens |
| 6, 7 | echoes | | opposite sides |

On a mono board the same mix collapses to one speaker; nothing depends on
stereo.

## Effects

- **Position.** An effect is panned by the screen x of its source: a
  teppistello bouncing off the shield on the right is heard on the right.
- **Cave echo.** Below ground every effect is played again 6 frames later
  at half volume on the opposite side, and 13 frames later, an octave down
  and at a quarter, back on the first side.
- **Written echo.** In the subterranean tune the lead answers each bell two
  ticks late from the other speaker, an octave below.
- **Scrubbing** is short noise bursts at random pitch every fourth frame;
  **the hum** of the shield is a note held on its own channel and released
  with its envelope; **drips** fall at random positions in the dark.

## Tunes

| Track | Use | Notes |
| ---: | --- | --- |
| 0 | title | a tarantella in A minor, counted in six |
| 1 | streets | D minor patrol groove, sixteen ticks to the bar |
| 2 | cavities | slow D minor, bells and their echo |
| 3 | a rione reaches 100 | C major fanfare, plays once, then the layer tune resumes |
| 4 | ending | the tarantella, slower, turned to A major |

All tunes are original. Notation: `NAME[:TICKS]`, `-` for a rest; a tick
is the tracker's sixteenth note.
