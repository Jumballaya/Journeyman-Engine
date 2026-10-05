"""Cuts the third-party sheets in tools/vendor/ (see CREDITS.md) into the
frames the game uses, at the sizes the hand-made art had."""
import os

from PIL import Image

VENDOR = os.path.join(os.path.dirname(__file__), "vendor")

# Antifarea's sheet: 18x20 frames; each character is 12 columns by 3 rows
# (up, side facing left, down), two characters side by side per 60px band.
FRAME_W, FRAME_H = 18, 20
CAST = {"kael": (1, 0), "lyra": (2, 1), "bram": (3, 0),
        "elder": (19, 0), "inn": (16, 1), "smith": (7, 0), "child": (12, 0)}  # (band, half)
ROWS = {"up": 0, "side": 1, "down": 2}
STEP_A, STAND, STEP_B = 0, 1, 2
POSE_COLUMN = {"idle": STAND, "attack": 5, "hurt": 7, "cast": 10}

_sheets = {}


def sheet(name, key=None):
    """A vendor PNG as RGBA; `key` is a background color made transparent."""
    if name not in _sheets:
        img = Image.open(os.path.join(VENDOR, name)).convert("RGBA")
        if key:
            img.putdata([(0, 0, 0, 0) if p[:3] == key else p for p in img.getdata()])
        _sheets[name] = img
    return _sheets[name]


def _frame(who, row, column):
    band, half = CAST[who]
    x = half * 12 * FRAME_W + column * FRAME_W
    y = band * 3 * FRAME_H + ROWS[row] * FRAME_H
    img = sheet("antifarea_charset_18x20.png").crop((x, y, x + FRAME_W, y + FRAME_H))
    return img.transpose(Image.FLIP_LEFT_RIGHT) if row == "side" else img  # our art faces right


def _stand(img, w, h):
    """`img` placed in a w x h frame, centered, keeping its feet on the bottom row."""
    out = Image.new("RGBA", (w, h))
    left = (w - FRAME_W) // 2
    out.paste(img, (left, h - FRAME_H), img)
    return out


def walker(who, facing, frame):
    """A 16x24 map figure; frame 0 stands, 1 and 2 are the two steps."""
    column = (STAND, STEP_A, STEP_B)[frame]
    img = _frame(who, facing, column)
    box = img.getbbox()
    cx = (box[0] + box[2]) // 2
    left = max(0, min(FRAME_W - 16, cx - 8))
    return _stand(img.crop((left, 0, left + 16, FRAME_H)), 16, 24)


def battler(who, pose):
    """A 24x32 battle figure facing right."""
    return _stand(_frame(who, "side", POSE_COLUMN[pose]), 24, 32)


def knocked_out(who):
    """The standing figure lying on its back, 32x32."""
    img = _frame(who, "down", STAND).rotate(90, expand=True)
    img = img.crop(img.getbbox())
    out = Image.new("RGBA", (32, 32))
    out.paste(img, ((32 - img.width) // 2, 32 - img.height), img)
    return out


def _cut(name, box, key=None):
    """A sprite cut from a sheet, padded to even sides so it lands on whole pixels."""
    img = sheet(name, key).crop(box)
    out = Image.new("RGBA", (img.width + img.width % 2, img.height + img.height % 2))
    out.paste(img, (0, out.height - img.height))
    return out


def _bob(img):
    """A second idle frame: the sprite one pixel lower."""
    out = Image.new("RGBA", img.size)
    out.paste(img.crop((0, 0, img.width, img.height - 1)), (0, 1))
    return out


def monsters():
    """Every enemy's idle frames, facing right: {name: [frames]}."""
    jelly = _cut("redshrike_rpgcritters2.png", (62, 199, 90, 221))
    goblin = _cut("redshrike_goblins2.png", (13, 50, 50, 86), key=(0, 128, 0))
    wyrm = _cut("redshrike_dragons.png", (110, 108, 287, 215))
    skull = "redshrike_rpgcritter_update.png"
    wisp = [_cut(skull, (192 + 16 * i, 66, 208 + 16 * i, 96), key=(255, 0, 255)) for i in range(3)]
    return {"jelly": [jelly, _bob(jelly)], "goblin": [goblin, _bob(goblin)],
            "wisp": wisp, "wyrm": [wyrm, _bob(wyrm)]}


def backdrop(name, size, top, tint=None):
    """A painted battle background brought down to the game's resolution and
    a 48-color palette so it sits with the pixel art; `top` crops the sky."""
    w, h = size
    img = Image.open(os.path.join(VENDOR, name)).convert("RGB")
    scaled = img.resize((w, round(img.height * w / img.width)), Image.LANCZOS)
    scaled = scaled.crop((0, top, w, top + h))
    if tint:
        scaled = Image.merge("RGB", [band.point(lambda v, k=k: min(255, int(v * k)))
                                     for band, k in zip(scaled.split(), tint)])
    return scaled.quantize(48, dither=Image.Dither.NONE).convert("RGBA")
