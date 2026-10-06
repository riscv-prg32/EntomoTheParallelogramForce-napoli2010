# AGENTS.md

Guidance for automated coding agents and maintainers of Entomo: The
Parallelogram Force.

## Project intent

A PRG32 cartridge that is a tribute to Entomo, the masked civic hero of
Naples, a teaching example of software scrolling with parallax on RV32IMC,
and a game about restorative direct action. The events are fiction; the
streets are real.

## Rules that are easy to break

- **Non-violence is the design, not a setting.** No combat, no health, no
  punishment, no shaming of citizens, no carceral language in any text.
  Contact with the shield is an elastic bounce (`velocity = -velocity * 2`);
  contact without it costs clock time only. At Decoro 100 the troublemakers
  become citizens. Do not add a damage path "for balance".
- **The surface map comes from the atlas.** Do not edit
  `assets/maps/surface.map` by hand; change `tools/build_map.py` and re-run
  it on the atlas PDF. The PDF and its renders are copyrighted: never
  commit them. Only the derived grid is committed.
- **The two maps share coordinates.** A shaft is tile type 2 on the same
  tile in both layers; `tools/build_underground.py` writes both and
  `tests/run_tests.sh` checks it. Run it after `build_map.py`.
- **Generated files are committed and must be current:** `src/gen_assets.h`
  (`tools/build_assets.py`), `audio/audio.json` (`tools/build_audio.py`),
  `assets/store/*` (`tools/make_store_art.py`), `dist/*`.
- **One source per build:** `src/entomo.c` includes `gen_assets.h`; the
  builder takes one file. Do not add compiler flags.
- **No pointers in initialised static data** (portable cartridges have no
  relocations): names are fixed-size `char` arrays, tables hold numbers.
  `tools/check_relocatable.py` enforces it. No libc, no floats, no 64-bit
  division.
- **Text uses only `A-Z 0-9 ? ! . , : - + /` and space**, at most 40
  characters per line; the host tests fail otherwise.
- **Every colour goes through the palette** (`col(index)`), never a raw
  RGB565 literal: on the indexed LCD a raw colour can land in a palette
  cell the game has redefined. The three display models are explained above
  `detect_display()`; the host tests compare them pixel by pixel.
- **Music owns channels 0-3** (the tracker plays instrument n on channel n);
  effects use 4-7. Never add recorded or third-party samples.
- **Budgets** are checked by `scripts/build.sh`: 64 KiB package, 64 KiB
  executable RAM, 54.5 KB load image. The Store screenshot is sized to fit.
- **Do not publish** to the Cartridge Store from scripts; publication is a
  human-approved, authenticated step. No credentials in the repository.
- **Do not modify the PRG32 checkout**; if a firmware bug is found, describe
  it and ask.

## Workflow

```bash
tests/run_tests.sh shots
python3 tools/make_store_art.py
scripts/build.sh && scripts/pack-store-bundle.sh
scripts/check_hosts.sh
```

Update `README.md`, `CHANGELOG.md` and the relevant `docs/*.md` (including
the budgets and the bot result they quote) whenever gameplay, maps, audio
or budgets change.
