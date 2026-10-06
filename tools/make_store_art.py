#!/usr/bin/env python3
"""Make the Cartridge Store icon and screenshot.

  assets/store/icon.png        64x64: Entomo inside the parallelogram
  assets/store/screenshot.png  320x200 gameplay frame

The icon is drawn from the same bust and palette as the title screen
(tools/build_assets.py). The screenshot is a frame of the unmodified
cartridge source on the host harness (build/shots/store.png, written by
`tests/run_tests.sh shots`): Via Speranzella with the shield up. It is
deterministic, unlike a capture of a running emulator.

Both images are palettised, and the screenshot uses the largest palette
that keeps it under SCREENSHOT_BUDGET bytes, because the Store trailer
counts towards the 64 KiB cartridge slot.
"""
from __future__ import annotations

import io
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_assets  # noqa: E402

SCREENSHOT_BUDGET = 8500


def icon() -> Image.Image:
    pal = build_assets.PALETTES[2]
    navy, neon, dark = pal[1], pal[10], pal[12]
    img = Image.new("RGB", (64, 64), navy)
    draw = ImageDraw.Draw(img)
    shape = [(14, 2), (62, 2), (50, 61), (2, 61)]          # slope 2:1, as in the game
    draw.polygon(shape, fill=(24, 40, 60))
    for k in range(1, 6):                                   # the wireframe mesh
        x = 14 + k * 8
        draw.line([(x, 2), (x - 12, 61)], fill=dark)
        draw.line([(14 - k * 2, 2 + k * 10), (62 - k * 2, 2 + k * 10)], fill=dark)
    bust = build_assets.portrait()
    for y in range(60):
        for x in range(48):
            v = bust[y][x]
            if v:
                img.putpixel((8 + x, 3 + y), pal[v])
    draw.line(shape + [shape[0]], fill=neon, width=2)
    return img


def main() -> int:
    out = ROOT / "assets/store"
    out.mkdir(parents=True, exist_ok=True)
    icon().quantize(colors=16).save(out / "icon.png", optimize=True)
    source = ROOT / "build/shots/store.png"
    if not source.exists():
        raise SystemExit("no screenshot source: run tests/run_tests.sh shots first")
    frame = Image.open(source).convert("RGB")
    assert frame.size == (320, 200), frame.size
    for colours in (64, 48, 32, 24, 16, 12, 8):
        buf = io.BytesIO()
        frame.quantize(colors=colours, dither=Image.Dither.NONE).save(buf, format="PNG", optimize=True)
        if buf.tell() <= SCREENSHOT_BUDGET:
            break
    (out / "screenshot.png").write_bytes(buf.getvalue())
    print(f"screenshot: {colours} colours")
    for f in sorted(out.glob("*.png")):
        print(f"{f.relative_to(ROOT)}: {f.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
