# Testing

## Host tests

```bash
tests/run_tests.sh          # checks only
tests/run_tests.sh shots    # also build/shots/*.png
```

`tests/test_game.c` includes the unmodified `src/entomo.c` over
`tests/host/host_prg32.h`, a model of the PRG32 calls the game uses,
compiled with the real PRG32 headers, AddressSanitizer and UBSan.

| Test | What it proves |
| --- | --- |
| tables | shafts coincide in both layers; jobs and actors stand on walkable tiles |
| title and walk | title, start, movement, 600 frames of erratic input never put the hero or an actor in a wall, atlas opens and closes |
| layers | SELECT swaps the map pointer, coordinates are continuous, music changes, an effect below ground has two echoes from alternate sides |
| shield | bounce: velocity inverted and doubled, score, no time lost; FORZA runs out and recharges; a contact costs time and nothing else |
| decoro | scrubbing is progressive and audible; a full rione turns all its troublemakers into citizens; four rioni win; the score is saved once |
| dawn | the clock ends the patrol |
| palette | the three display models show identical pixels |
| **bot** | a breadth-first bot plays a whole night through the controller alone: all 64 jobs, 14 layer changes, done at 01:02 game time (182 of 480 minutes) |
| consistency | versions, map sizes, shafts, note ranges |

The harness also fails on any character outside the portable set and on
text that runs off the screen.

## Emulators

```bash
scripts/check_hosts.sh
```

1. position independence: no absolute relocations, and the image linked at
   two addresses is byte-identical;
2. PRG32-QT core (`prg32qt-headless`), 300 frames with scripted input;
3. PRG32 QEMU firmware (`scripts/qemu_preview.py --script smoke`): the
   cartridge loads and the console shows no panic.

`scripts/qemu_preview.py --script preview --video` records the
preview (up to 30 seconds) from the QEMU firmware framebuffer and its PCM stream.

## On the board

Not run here; check by hand after `python3 -m prg32 esp32c6 upload`:

- colours match the QEMU capture (the indexed-display path);
- scrolling is smooth; the playfield redraws 304x176 pixels every frame;
- with two MAX98357A boards: bass centre, lead left, bells and percussion
  right, a bounce on the right is heard on the right, echoes below ground;
- the status bands show the title and "NAPOLI 2010 - PRG32";
- A on the ending opens the score table.
