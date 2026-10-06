#!/usr/bin/env bash
# Build the Entomo cartridges for every PRG32 target.
#
#   scripts/build.sh            # esp32c6 + qemu
#   scripts/build.sh qemu       # one architecture
#
# Environment:
#   PRG32_REPO   path to the PRG32 repository (default ../PRG32)
#   IDF_EXPORT   ESP-IDF export script, sourced when riscv32-esp-elf-gcc is
#                neither on PATH nor under ~/.espressif (default
#                $HOME/esp-idf/export.sh)
#
# Outputs dist/entomo-<arch>.prg32 (Store-ready: metadata, icon, screenshot
# and colophon attached), dist/SHA256SUMS, and prints the budgets.
#
# The street atlas is not needed here: the maps derived from it are
# committed (assets/maps/*.map). See docs/maps.md to regenerate them.
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prg32_repo="$(cd "${PRG32_REPO:-"$repo_dir/../PRG32"}" && pwd)"
name="entomo"
cart_ram_kib=64          # the PRG32 default (extended) profile
package_limit=65536      # one cartridge slot: code + audio + Store trailer
load_limit=54500         # measured QEMU limit for header + code/data + audio

if ! command -v riscv32-esp-elf-gcc >/dev/null 2>&1; then
  # The toolchain alone is enough; prefer it to the whole ESP-IDF environment.
  toolchain="$(ls -d "$HOME"/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin 2>/dev/null | tail -1 || true)"
  if [[ -n "$toolchain" ]]; then
    export PATH="$toolchain:$PATH"
  else
    idf_export="${IDF_EXPORT:-$HOME/esp-idf/export.sh}"
    # shellcheck disable=SC1090
    [[ -f "$idf_export" ]] && source "$idf_export" >/dev/null 2>&1
  fi
fi
command -v riscv32-esp-elf-gcc >/dev/null 2>&1 || {
  echo "error: riscv32-esp-elf-gcc not found; install ESP-IDF or source its export.sh" >&2
  exit 2
}

archs=("$@")
[[ ${#archs[@]} -eq 0 ]] && archs=(esp32c6 qemu)

build_dir="$repo_dir/build"
dist_dir="$repo_dir/dist"
mkdir -p "$build_dir" "$dist_dir"

prg32() { (cd "$prg32_repo" && python3 -m prg32 "$@"); }

# 1. Regenerate the tables and the soundtrack (deterministic; committed).
python3 "$repo_dir/tools/build_assets.py"
python3 "$repo_dir/tools/build_audio.py"

# 2. AUDIO block: fourteen SID-like instruments, five tracks, no samples.
python3 "$prg32_repo/tools/prg32audio_pack.py" "$repo_dir/audio/audio.json" \
  --out "$build_dir/audio.block"

# 3. Prove position independence (no relocations: hosts load anywhere).
PRG32_REPO="$prg32_repo" python3 "$repo_dir/tools/check_relocatable.py" | tail -1

for arch in "${archs[@]}"; do
  raw="$build_dir/$name-$arch.raw.prg32"
  out="$dist_dir/$name-$arch.prg32"
  log="$build_dir/$name-$arch.build.log"
  # 4. Portable cartridge. Nothing is *required*: without audio the game is
  # silent, and stereo only widens what mono already plays.
  prg32 cartridge build "$repo_dir/src/entomo.c" \
    --portable \
    --optional-feature audio \
    --optional-feature audio_plus \
    --optional-feature sprites \
    --entry-prefix entomo \
    --name "$name" \
    --architecture "$arch" \
    --cart-ram-kib "$cart_ram_kib" \
    --audio-block "$build_dir/audio.block" \
    --out "$raw" > "$log" 2>&1 || { cat "$log" >&2; exit 1; }
  read -r code mem audio < <(prg32 cartridge summary "$raw" | python3 -c "
import json, sys
d = json.load(sys.stdin)
print(d['code_size'], d['mem_size'], d['legacy_size'] - d['code_size'] - d['header_size'])")
  load=$(( code + audio + 128 ))

  # 5. Store trailer: metadata, icon, screenshot and colophon.
  prg32 store attach-metadata "$raw" --out "$out" \
    --metadata "$repo_dir/metadata/metadata.json" \
    --icon "$repo_dir/assets/store/icon.png" \
    --screenshot "$repo_dir/assets/store/screenshot.png" \
    --colophon "$repo_dir/metadata/colophon.json" \
    --architecture "$arch" >/dev/null

  size=$(wc -c < "$out" | tr -d ' ')
  echo "$out: package $size/$package_limit, RAM $mem/$(( cart_ram_kib * 1024 )), load ~$load/$load_limit (code $code, audio $audio)"
  (( size <= package_limit )) || { echo "error: package too large" >&2; exit 1; }
  (( mem <= cart_ram_kib * 1024 )) || { echo "error: executable RAM exceeded" >&2; exit 1; }
  (( load <= load_limit )) || { echo "error: load image too large for QEMU" >&2; exit 1; }
done

# 6. Inspect what was produced.
for f in "$dist_dir"/$name-*.prg32; do
  prg32 cartridge summary "$f" > "$build_dir/$(basename "$f").summary.txt"
  prg32 store inspect-metadata "$f" > "$build_dir/$(basename "$f").metadata.txt"
done
(cd "$dist_dir" && shasum -a 256 $name-*.prg32 > SHA256SUMS)
echo "summaries in build/*.summary.txt; checksums in dist/SHA256SUMS"
