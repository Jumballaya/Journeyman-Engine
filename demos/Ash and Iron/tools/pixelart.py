"""A tiny SNES-style pixel art renderer: paint shapes as *materials*, then
render() shades every material with its colour ramp (light from the top
left, dithered between steps) and draws a coloured outline around it."""
import math

from PIL import Image


def ramp(*hexes):
    """Colours from darkest to lightest, e.g. ramp("#203", "#406", "#80a", "#c4e")."""
    out = []
    for h in hexes:
        h = h.lstrip("#")
        if len(h) == 3:
            h = "".join(c * 2 for c in h)
        out.append(tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)))
    return out


class Material:
    def __init__(self, colors, shade=True, outline=True, light=0.0):
        self.colors = colors      # dark -> light
        self.shade = shade        # False: flat (eyes, glints)
        self.outline = outline    # draw an outline where it meets transparency
        self.light = light        # extra brightness, -1..1


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[None] * w for _ in range(h)]
        self.tone = [[0.0] * w for _ in range(h)]   # per-pixel brightness nudge

    # ---- painting --------------------------------------------------------------
    def set(self, x, y, m, tone=0.0):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = m
            self.tone[y][x] = tone

    def rect(self, x0, y0, x1, y1, m, tone=0.0):
        for y in range(int(y0), int(y1)):
            for x in range(int(x0), int(x1)):
                self.set(x, y, m, tone)

    def ellipse(self, cx, cy, rx, ry, m, tone=0.0):
        for y in range(int(cy - ry - 1), int(cy + ry + 2)):
            for x in range(int(cx - rx - 1), int(cx + rx + 2)):
                if ((x + 0.5 - cx) / max(rx, 0.01)) ** 2 + ((y + 0.5 - cy) / max(ry, 0.01)) ** 2 <= 1:
                    self.set(x, y, m, tone)

    def poly(self, pts, m, tone=0.0):
        ys = [p[1] for p in pts]
        for y in range(int(min(ys)), int(max(ys)) + 1):
            yc = y + 0.5
            xs = []
            for i in range(len(pts)):
                (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
                if (y0 <= yc < y1) or (y1 <= yc < y0):
                    xs.append(x0 + (yc - y0) * (x1 - x0) / (y1 - y0))
            xs.sort()
            for a, b in zip(xs[0::2], xs[1::2]):
                for x in range(int(math.floor(a + 0.5)), int(math.floor(b + 0.5))):
                    self.set(x, y, m, tone)

    def line(self, x0, y0, x1, y1, m, width=1.0, tone=0.0):
        steps = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
        for i in range(steps + 1):
            t = i / steps
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            if width <= 1:
                self.set(x, y, m, tone)
            else:
                self.ellipse(x, y, width / 2, width / 2, m, tone)

    def limb(self, x0, y0, angle, length, width, m, tone=0.0):
        """A limb from (x0, y0) at `angle` degrees (0 = down, 90 = forward/right); returns its end."""
        a = math.radians(angle)
        x1, y1 = x0 + math.sin(a) * length, y0 + math.cos(a) * length
        self.line(x0, y0, x1, y1, m, width, tone)
        return x1, y1

    def blit(self, other, ox, oy):
        for y in range(other.h):
            for x in range(other.w):
                if other.px[y][x] is not None:
                    self.set(ox + x, oy + y, other.px[y][x], other.tone[y][x])

    def flipped(self):
        c = Canvas(self.w, self.h)
        for y in range(self.h):
            c.px[y] = self.px[y][::-1]
            c.tone[y] = self.tone[y][::-1]
        return c

    def rotated(self):
        """A quarter turn clockwise (for knocked-out poses)."""
        c = Canvas(self.h, self.w)
        for y in range(self.h):
            for x in range(self.w):
                c.px[x][self.h - 1 - y] = self.px[y][x]
                c.tone[x][self.h - 1 - y] = self.tone[y][x]
        return c

    # ---- rendering -------------------------------------------------------------
    def render(self, outline=True, shadow_color=(18, 14, 26)):
        img = Image.new("RGBA", (self.w, self.h))
        P = img.load()
        same = lambda x, y, m: 0 <= x < self.w and 0 <= y < self.h and self.px[y][x] is m
        for y in range(self.h):
            for x in range(self.w):
                m = self.px[y][x]
                if m is None:
                    continue
                n = len(m.colors)
                if not m.shade:
                    level = n - 2 + self.tone[y][x]
                else:
                    level = (n - 1) * 0.55 + m.light + self.tone[y][x]
                    if not same(x, y - 1, m): level += 1.0     # lit top edge
                    if not same(x - 1, y, m): level += 0.5     # lit left edge
                    if not same(x, y + 1, m): level -= 1.0     # shaded underside
                    if not same(x + 1, y, m): level -= 0.6     # shaded right side
                    if not same(x, y + 2, m): level -= 0.35
                frac = level - math.floor(level)
                base = int(math.floor(level))
                if 0.35 < frac < 0.65 and (x + y) % 2 == 0:     # dither between steps
                    base += 1
                elif frac >= 0.65:
                    base += 1
                c = m.colors[max(0, min(n - 1, base))]
                P[x, y] = c + (255,)
        if outline:
            for y in range(self.h):
                for x in range(self.w):
                    if self.px[y][x] is not None:
                        continue
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < self.w and 0 <= ny < self.h:
                            m = self.px[ny][nx]
                            if m is not None and m.outline:
                                d = m.colors[0]
                                P[x, y] = tuple(int(d[i] * 0.45 + shadow_color[i] * 0.55) for i in range(3)) + (255,)
                                break
        return img
