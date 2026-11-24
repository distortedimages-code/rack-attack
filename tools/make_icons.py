#!/usr/bin/env python3
"""The app icon: the pixel mascot of the sequencer page, white with its red mouth, on a black rounded square.

Writes Resources/icon/RackAttack-1024.png (the app, AU, VST3: CMake ICON_BIG), site/apple-touch-icon.png
(iPad / iPhone home screen) and site/favicon.svg (a crisp 16 x 16 version). Needs Pillow.
"""
import os
from PIL import Image, ImageDraw

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
WHITE, RED, YELLOW = (255, 255, 255, 255), (255, 59, 47, 255), (255, 176, 0, 255)
SHADOW, SHADOW2, EDGE, BLACK = (42, 42, 42, 255), (28, 28, 28, 255), (42, 42, 42, 255), (0, 0, 0, 255)

# the mascot, as Pixel.cpp draws it (X white, R red)
MASCOT = ["...XXXX...", ".XXXXXXXX.", "XXXXXXXXXX", "XX.XXXX.XX", "XXXXXXXXXX", "XXXRRRRXXX", ".XXXXXXXX.", ".X.X..X.X."]

def grid32():
    """The icon's 32 x 32 pixels: the mascot twice as big, its shadow, two yellow ticks on each side (it's excited)."""
    px = {}
    for y, row in enumerate (MASCOT):
        for x, ch in enumerate (row):
            if ch == '.':
                continue
            for dy in range (2):
                for dx in range (2):
                    px[(6 + x * 2 + dx, 6 + y * 2 + dy)] = RED if ch == 'R' else WHITE
    for x in range (9, 23):
        px[(x, 25)] = SHADOW
    for x in range (11, 21):
        px[(x, 26)] = SHADOW2
    for p in [(3, 9), (2, 8), (3, 12), (2, 12), (28, 9), (29, 8), (28, 12), (29, 12)]:
        px[p] = YELLOW
    return px

def app_icon (size = 1024):
    """macOS style (Apple's grid): a rounded square of 824 px out of 1024, transparent around it."""
    unit = size * 24 // 1024                     # 24 px pixels at 1024
    off = (size - unit * 32) // 2
    inset, radius = size * 100 // 1024, size * 185 // 1024
    big = 4                                      # the rounded square drawn 4 x bigger, then reduced: smooth edges
    box = [inset * big, inset * big, (size - inset) * big, (size - inset) * big]
    mask = Image.new ("L", (size * big, size * big), 0)
    ImageDraw.Draw (mask).rounded_rectangle (box, radius = radius * big, fill = 255)
    mask = mask.resize ((size, size), Image.LANCZOS)
    img = Image.new ("RGBA", (size, size), (0, 0, 0, 0))
    img.paste (Image.new ("RGBA", (size, size), BLACK), (0, 0), mask)
    edge = Image.new ("L", (size * big, size * big), 0)
    ImageDraw.Draw (edge).rounded_rectangle (box, radius = radius * big, outline = 255, width = max (1, size * 8 // 1024) * big)
    edge = edge.resize ((size, size), Image.LANCZOS)
    img.paste (Image.new ("RGBA", (size, size), EDGE), (0, 0), edge)
    d = ImageDraw.Draw (img)
    for (x, y), c in grid32().items():
        d.rectangle ([off + x * unit, off + y * unit, off + (x + 1) * unit - 1, off + (y + 1) * unit - 1], fill = c)
    return img

def touch_icon (size = 180):
    """iOS rounds it itself: full black square, the pixels a little bigger."""
    unit = size // 36
    art = unit * 32
    off = (size - art) // 2
    img = Image.new ("RGBA", (size, size), BLACK)
    d = ImageDraw.Draw (img)
    for (x, y), c in grid32().items():
        d.rectangle ([off + x * unit, off + y * unit, off + (x + 1) * unit - 1, off + (y + 1) * unit - 1], fill = c)
    return img

def favicon_svg():
    """16 x 16: the mascot at its own size (1 pixel = 1 pixel), crisp in a browser tab."""
    rects = []
    for y, row in enumerate (MASCOT):
        for x, ch in enumerate (row):
            if ch != '.':
                rects.append ('<rect x="%d" y="%d" width="1" height="1" fill="%s"/>' % (3 + x, 4 + y, "#ff3b2f" if ch == 'R' else "#fff"))
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" shape-rendering="crispEdges">'
            '<rect width="16" height="16" rx="3" fill="#000"/>' + "".join (rects) + '</svg>\n')

if __name__ == "__main__":
    app_icon().save (os.path.join (ROOT, "Resources", "icon", "RackAttack-1024.png"))
    touch_icon().save (os.path.join (ROOT, "site", "apple-touch-icon.png"))
    with open (os.path.join (ROOT, "site", "favicon.svg"), "w") as f:
        f.write (favicon_svg())
    print ("ok")
