#!/usr/bin/env python3
"""Draws the DMG window's background (tools/dmg/background.png and background@2x.png): light grey
paper, black pixels, a red dot, the ghost. Light because Finder writes the icon names in black.
The icon places it is drawn around are those of dmg_settings.py (keep them together).
Needs Pillow. Run: python3 tools/dmg/draw_background.py"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, '..', '..', 'Resources', 'fonts')
W, H = 720, 520
PAPER, INK, MID, DOT, RED, YEL = '#E4E4E4', '#111111', '#6A6A6A', '#CFCFCF', '#FF3B2F', '#FFB000'
PKG, README = (150, 235), (150, 420)
ROWS = [(420, 180, 610, 'AU'), (420, 290, 610, 'VST3'), (420, 400, 610, 'APPLI')]
GHOST = ["...####...", ".########.", "##.##.####", "##########", "##########", "####RR####", "##########", "#.##.##.##"]


def draw(s):
    im = Image.new('RGB', (W * s, H * s), PAPER)
    d = ImageDraw.Draw(im)
    S = lambda v: int(round(v * s))
    big = ImageFont.truetype(os.path.join(FONTS, 'Silkscreen-Bold.ttf'), S(26))
    head = ImageFont.truetype(os.path.join(FONTS, 'Silkscreen-Bold.ttf'), S(11))
    txt = ImageFont.truetype(os.path.join(FONTS, 'VT323-Regular.ttf'), S(21))
    small = ImageFont.truetype(os.path.join(FONTS, 'VT323-Regular.ttf'), S(18))

    # a sparse dot screen, like the app's dithers
    for y in range(0, H * s, S(4)):
        for x in range((y // S(4)) % 2 * S(2), W * s, S(4)):
            d.rectangle((x, y, x + max(0, s - 1), y + max(0, s - 1)), fill=DOT)

    # title
    x = S(36)
    for part, col in (('RACK', INK), ('·', RED), ('ATTACK', INK), (' EDITOR', INK)):
        d.text((x, S(26)), part, font=big, fill=col)
        x += d.textlength(part, font=big)
    d.text((S(38), S(64)), 'éditeur · séquenceur · simulateur — pour le Waldorf RackAttack', font=small, fill=MID)

    # the ghost
    p = S(5)
    gx, gy = S(W - 36) - 10 * p, S(22)
    for r, row in enumerate(GHOST):
        for c, ch in enumerate(row):
            if ch != '.':
                d.rectangle((gx + c * p, gy + r * p, gx + (c + 1) * p - 1, gy + (r + 1) * p - 1), fill=RED if ch == 'R' else INK)

    # line
    for xx in range(S(24), S(W - 24), S(4)):
        d.rectangle((xx, S(96), xx + S(2) - 1, S(97)), fill=INK)

    def panel(x0, y0, x1, y1, title):
        d.rectangle((S(x0), S(y0), S(x1), S(y1)), outline=INK, width=S(3))
        d.rectangle((S(x0), S(y0), S(x1), S(y0 + 24)), fill=INK)
        d.text((S(x0 + 10), S(y0 + 5)), title, font=head, fill=PAPER)

    # left: the installer
    panel(36, 116, 264, 484, 'LE PLUS SIMPLE')
    d.text((S(PKG[0]) - d.textlength('DOUBLE-CLIC', font=txt) / 2, S(PKG[1] - 82)), 'DOUBLE-CLIC', font=txt, fill=INK)
    # a small pixel arrow down to the installer
    for i in range(5):
        d.rectangle((S(PKG[0] - 8 + i * 2), S(PKG[1] - 58 + i * 3), S(PKG[0] + 8 - i * 2), S(PKG[1] - 56 + i * 3)), fill=RED)
    d.text((S(PKG[0]) - d.textlength('macOS refuse ?', font=small) / 2, S(README[1] - 74)), 'macOS refuse ?', font=small, fill=MID)
    d.text((S(PKG[0]) - d.textlength('la commande est ici', font=small) / 2, S(README[1] - 58)), 'la commande est ici', font=small, fill=MID)

    # right: by hand
    panel(290, 116, 690, 484, 'OU GLISSE CHAQUE ÉLÉMENT SUR SON DOSSIER')
    for fx, fy, tx, name in ROWS:
        # dotted arrow from the bundle to its folder
        for xx in range(S(fx + 46), S(tx - 50), S(8)):
            d.rectangle((xx, S(fy - 2), xx + S(4), S(fy + 1)), fill=INK)
        for i in range(5):
            d.rectangle((S(tx - 52 + i * 2), S(fy - 8 + i * 2), S(tx - 50 + i * 2), S(fy + 8 - i * 2)), fill=INK)
        d.text((S(305), S(fy - 10)), name, font=head, fill=RED)
    d.text((S(304), S(458)), 'puis la commande du LISEZ-MOI dans le Terminal', font=small, fill=MID)
    return im


draw(1).save(os.path.join(HERE, 'background.png'))
draw(2).save(os.path.join(HERE, 'background@2x.png'))
print('drawn')
