# Cartridge Store

```bash
tests/run_tests.sh shots
python3 tools/make_store_art.py
scripts/build.sh
scripts/pack-store-bundle.sh
```

produces `dist/entomo-1.0.0-store.zip`:

```text
manifest.json            prg32-metadata-1.0, with the colophon embedded
icon.png                 64x64
splash.png               320x200 gameplay frame
entomo-esp32c6.prg32     ESP32-C6 variant
entomo-qemu.prg32        QEMU variant
```

The manifest is generated from `metadata/metadata.json` and
`metadata/colophon.json`, the single source of truth. Each `.prg32` is
also Store-ready on its own (metadata, icon, screenshot and colophon in
its trailer) and fits one 64 KiB slot. `tools/validate_bundle.py` checks
the bundle; with a CartridgeStore checkout next to this repository it uses
the Store's own ingestion code.

`assets/store/preview.mp4` is the audiovisual preview captured from the
QEMU firmware.

Publishing is a separate, authenticated, human step. No script in this
repository contacts a Store, and no credentials belong here. To publish,
upload the bundle from the Store's publisher page, or with the PRG32
tooling once logged in:

```bash
cd ../PRG32
python3 -m prg32 store publish-bundle --help
```
