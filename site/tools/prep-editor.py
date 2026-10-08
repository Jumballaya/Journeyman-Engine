#!/usr/bin/env python3 -I
"""Turn raw 3200x2000 editor captures into the site's editor images.

Usage: site/tools/prep-editor.py <raw dir> <suffix> [--light]
Reads <raw dir>/<shot>-<suffix>.png and writes src/img/editor/<name>.jpg, or
<name>-light.jpg with --light. Both themes go through the same crop boxes, so
their pixels line up and the site can swap one for the other in place.
"""
import sys
from pathlib import Path
from PIL import Image

# shot -> [(output name, crop box as fractions of the frame or None for the whole frame)]
CROPS = {
    "scene": [("scene", None)],
    "tiles": [("tiles-crop", (0.18, 0.03, 1.0, 0.72))],
    "data": [("data-crop", (0.18, 0.03, 1.0, 0.70))],
    "ui": [("ui-crop", (0.18, 0.04, 1.0, 0.86))],
    "play": [("play-crop", (0.18, 0.04, 0.90, 0.88))],
}

raw, suffix = Path(sys.argv[1]), sys.argv[2]
light = "--light" in sys.argv
out = Path(__file__).resolve().parent.parent / "src/img/editor"
for shot, outputs in CROPS.items():
    im = Image.open(raw / f"{shot}-{suffix}.png").convert("RGB")
    W, H = im.size
    for name, box in outputs:
        part = im if box is None else im.crop((int(box[0] * W), int(box[1] * H), int(box[2] * W), int(box[3] * H)))
        part = part.resize((1600, round(part.size[1] * 1600 / part.size[0])), Image.LANCZOS)
        dest = out / f"{name}{'-light' if light else ''}.jpg"
        part.save(dest, quality=88)
        print(dest.name, part.size)
