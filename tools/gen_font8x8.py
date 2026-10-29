#!/usr/bin/env python3
"""Regenerate sources/wiiu/cwv_font8x8.h from assets/font.ttf.

Renders each printable ASCII char at 32px, tightly crops to the ink,
rescales into an 8x8 cell with a coverage threshold, and emits a C
table.  Used only when the TTF itself cannot be loaded on the Wii U
(no app root) so boot diagnostics always render.  Run from the repo
root: python3 tools/gen_font8x8.py
"""
from PIL import Image, ImageDraw, ImageFont
import sys

SRC = sys.argv[1] if len(sys.argv) > 1 else 'assets/font.ttf'
DST = sys.argv[2] if len(sys.argv) > 2 else 'sources/wiiu/cwv_font8x8.h'
RENDER = 32

font = ImageFont.truetype(SRC, RENDER)
glyphs = {}
for code in range(32, 127):
    img = Image.new('L', (RENDER * 2, RENDER * 2), 0)
    ImageDraw.Draw(img).text((RENDER // 2, RENDER // 2),
                             chr(code), font=font, fill=255)
    bbox = img.getbbox()          # tight ink box
    if not bbox:
        glyphs[code] = [0] * 8    # space / blank
        continue
    g = img.crop(bbox)
    # fit the ink into 8x8, preserving aspect: scale so the larger side
    # is 8 and center the other axis
    w, h = g.size
    scale = min(8.0 / w, 8.0 / h)
    nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
    g = g.resize((nw, nh))
    cell = Image.new('L', (8, 8), 0)
    cell.paste(g, ((8 - nw) // 2, 0))
    px = cell.load()
    rows = []
    for y in range(8):
        row = 0
        for x in range(8):
            if px[x, y] > 96:     # coverage threshold
                row |= (1 << x)
        rows.append(row)
    glyphs[code] = rows

out = ['/* cwv_font8x8.h - 8x8 ASCII fallback font (chars 32..126).\n',
       ' * GENERATED from %s by tools/gen_font8x8.py -\n' % SRC,
       ' * regenerate when the TTF changes.  Used ONLY when the TTF\n',
       ' * cannot be loaded (no app root), so boot diagnostics always\n',
       ' * render (bug report 2026-09-23 (35): "no text on start").\n',
       ' * Row order: bit 0 = leftmost pixel.  */\n',
       '#ifndef CWV_FONT8X8_H\n#define CWV_FONT8X8_H\n\n',
       'static const unsigned char cwv_font8x8[95][8] = {\n']
for code in range(32, 127):
    out.append('    { %s }, /* 0x%02X %s */\n' % (
        ', '.join('0x%02X' % r for r in glyphs[code]), code, chr(code)))
out.append('};\n\n#endif\n')
open(DST, 'w').write(''.join(out))
print('%s written (%d chars)' % (DST, len(''.join(out))))
