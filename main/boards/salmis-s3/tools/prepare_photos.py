#!/usr/bin/env python3
"""Prepares the photos of the microSD card for the Salmis slideshow.

The robot shows them on a 240x240 round screen. If a photo is prepared at 480x480 (a square
crop of its center) the robot only has to shrink it by half, so the result stays sharp.

Usage: prepare_photos.py SOURCE_DIR [DEST_DIR]
Needs Pillow. JPEG only: the firmware cannot decode PNG or progressive JPEG.
"""
import sys
from pathlib import Path

from PIL import Image

TARGET = 480
QUALITY = 88


def prepare(source: Path, destination: Path) -> int:
    destination.mkdir(parents=True, exist_ok=True)
    converted = 0
    for path in sorted(source.iterdir()):
        if path.is_dir() or path.name.startswith('.'):
            continue
        if path.suffix.lower() not in ('.jpg', '.jpeg'):
            print(f"SKIP {path.name}: only JPEG is supported")
            continue
        try:
            with Image.open(path) as image:
                image = image.convert('RGB')
                side = min(image.size)
                left = (image.width - side) // 2
                top = (image.height - side) // 2
                image = image.crop((left, top, left + side, top + side))
                if image.size[0] != TARGET:
                    image = image.resize((TARGET, TARGET), Image.LANCZOS)
                out = destination / (path.stem + '.jpg')
                image.save(out, 'JPEG', quality=QUALITY, optimize=True, progressive=False)
                print(f"OK {path.name} -> {out.name} ({out.stat().st_size // 1024} KB)")
                converted += 1
        except Exception as error:  # noqa: BLE001 - report and continue with the next file
            print(f"SKIP {path.name}: {error}")
    return converted


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    source = Path(sys.argv[1])
    destination = Path(sys.argv[2]) if len(sys.argv) > 2 else source
    total = prepare(source, destination)
    print(f"{total} photos prepared in {destination}")


if __name__ == '__main__':
    main()
