# Draws console/Resources/icon.png (the Undertow Rack app icon). Usage: python3 make_icon.py icon.png preview128.png
import math, sys
from PIL import Image, ImageDraw, ImageFilter
S = 4               # supersample
N = 1024 * S
img = Image.new("RGBA", (N, N), (0, 0, 0, 0))

# macOS-style tile: 824 px square, 185 px corner radius, centred, soft shadow
tile = 824 * S; r = 185 * S; x0 = (N - tile) // 2; y0 = x0 - 10 * S
shadow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
ImageDraw.Draw(shadow).rounded_rectangle([x0, y0 + 18 * S, x0 + tile, y0 + tile + 18 * S], r, fill=(0, 0, 0, 110))
img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(22 * S)))

# Vertical gradient body
grad = Image.new("RGBA", (N, N))
gd = ImageDraw.Draw(grad)
top, bot = (24, 52, 62), (9, 18, 22)
for y in range(y0, y0 + tile):
    t = (y - y0) / tile
    gd.line([(0, y), (N, y)], fill=tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)) + (255,))
mask = Image.new("L", (N, N), 0)
ImageDraw.Draw(mask).rounded_rectangle([x0, y0, x0 + tile, y0 + tile], r, fill=255)
img.paste(grad, (0, 0), mask)

# Three flowing waves (the rack's three slots / an undertow), brightest on top
layer = Image.new("RGBA", (N, N), (0, 0, 0, 0))
d = ImageDraw.Draw(layer)
teal = (63, 184, 175)
waves = [  # (centre y as fraction of tile, amplitude, phase, alpha, width)
    (0.34, 0.075, 0.0, 255, 62),
    (0.53, 0.060, 0.9, 170, 54),
    (0.71, 0.045, 1.8, 95, 46),
]
margin = 0.17
for cy, amp, ph, alpha, w in waves:
    pts = []
    for i in range(401):
        t = i / 400
        x = x0 + tile * (margin + (1 - 2 * margin) * t)
        y = y0 + tile * (cy + amp * math.sin(2 * math.pi * 1.25 * t + ph))
        pts.append((x, y))
    # Stamp discs along the path into a mask, so the stroke is smooth and evenly translucent
    m = Image.new("L", (N, N), 0)
    md = ImageDraw.Draw(m)
    rr = w * S / 2
    dense = []
    for i in range(len(pts) - 1):
        (ax, ay), (bx, by) = pts[i], pts[i + 1]
        steps = max(1, int(math.hypot(bx - ax, by - ay) / 4))
        for k in range(steps):
            dense.append((ax + (bx - ax) * k / steps, ay + (by - ay) * k / steps))
    dense.append(pts[-1])
    for (x, y) in dense:
        md.ellipse([x - rr, y - rr, x + rr, y + rr], fill=255)
    colour = Image.new("RGBA", (N, N), teal + (255,))
    colour.putalpha(m.point(lambda v: v * alpha // 255))
    layer.alpha_composite(colour)

img.alpha_composite(Image.composite(layer, Image.new("RGBA", (N, N), (0, 0, 0, 0)), mask))

# Faint top highlight for depth
hl = Image.new("RGBA", (N, N), (0, 0, 0, 0))
ImageDraw.Draw(hl).rounded_rectangle([x0, y0, x0 + tile, y0 + tile], r, outline=(255, 255, 255, 28), width=4 * S)
img.alpha_composite(hl)

img = img.resize((1024, 1024), Image.LANCZOS)
img.save(sys.argv[1])
img.resize((128, 128), Image.LANCZOS).save(sys.argv[2])
