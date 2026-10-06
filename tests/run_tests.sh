#!/usr/bin/env bash
# Host tests for Entomo (no RISC-V toolchain needed).
#
#   tests/run_tests.sh          gameplay tests, including a bot that plays a
#                               whole night through the controller
#   tests/run_tests.sh shots    also render the screens to build/shots/*.png
#                               (needs Pillow)
#
# PRG32_REPO points to the PRG32 checkout (default ../PRG32); its public
# headers are used, so a signature that differs from the firmware fails to
# compile.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prg32="$(cd "${PRG32_REPO:-$root/../PRG32}" && pwd)"
out="$root/build/tests"
mkdir -p "$out"
cflags=(-std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
        -I "$root/src" -I "$root/tests" -I "$root/tests/host"
        -I "$prg32/components/prg32/include" -I "$prg32/components/prg32_audio/include")

fail=0
# The generated tables must be the ones the tools produce today.
python3 "$root/tools/build_assets.py" >/dev/null
python3 "$root/tools/build_audio.py" >/dev/null
if ! git -C "$root" diff --quiet -- src/gen_assets.h audio/audio.json 2>/dev/null; then
  echo "note: src/gen_assets.h or audio/audio.json changed; commit the regenerated files"
fi

cc "${cflags[@]}" "$root/tests/test_game.c" -o "$out/test_game"
if [[ "${1:-}" == "shots" ]]; then
  shots="$root/build/shots"
  mkdir -p "$shots"
  "$out/test_game" "$shots" || fail=1
  python3 - "$shots" <<'PY'
import sys
from pathlib import Path
from PIL import Image
for ppm in sorted(Path(sys.argv[1]).glob("*.ppm")):
    Image.open(ppm).save(ppm.with_suffix(".png"))
    ppm.unlink()
    print(f"build/shots/{ppm.stem}.png")
PY
else
  "$out/test_game" || fail=1
fi

# Maps, metadata and soundtrack must be consistent with each other.
python3 - "$root" <<'PY' || fail=1
import json, sys
from pathlib import Path
root = Path(sys.argv[1])
meta = json.loads((root / "metadata/metadata.json").read_text(encoding="utf-8"))
colophon = json.loads((root / "metadata/colophon.json").read_text(encoding="utf-8"))
audio = json.loads((root / "audio/audio.json").read_text(encoding="utf-8"))
errors = []
if meta["version"] != colophon["version"]:
    errors.append("metadata and colophon versions differ")
if meta["abi"] != "prg32-metadata-1.0" or colophon["abi"] != "prg32-colophon-1.0":
    errors.append("unexpected ABI strings")
if meta["version"] not in (root / "CHANGELOG.md").read_text(encoding="utf-8"):
    errors.append("CHANGELOG.md does not mention the current version")
surface = (root / "assets/maps/surface.map").read_text().splitlines()
under = (root / "assets/maps/underground.map").read_text().splitlines()
for name, grid in (("surface", surface), ("underground", under)):
    if len(grid) != 128 or any(len(r) != 128 for r in grid):
        errors.append(f"{name}.map is not 128x128")
shafts = [(x, y) for y, r in enumerate(surface) for x, c in enumerate(r) if c == "O"]
if any(under[y][x] != "O" for x, y in shafts) or sum(r.count("O") for r in under) != len(shafts):
    errors.append("the shafts of the two layers do not coincide")
for t in audio["tracks"]:
    for e in t["events"]:
        if e["command"] == "NOTE_ON" and not (0 <= e["arg0"] < 4 and 12 <= e["arg1"] <= 120):
            errors.append(f"track {t['name']}: note out of range")
            break
for e in errors:
    print("FAIL consistency:", e)
print(f"consistency: version {meta['version']}, {len(shafts)} shafts, "
      f"{len(audio['instruments'])} instruments, {len(audio['tracks'])} tracks, {len(errors)} failures")
sys.exit(1 if errors else 0)
PY
exit $fail
