#!/usr/bin/env python3
# Builds src/worldland.inc (the globe look's land) from Natural Earth's 1:110m land:
#   curl -LO https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_land.geojson
#   python3 tools/worldland.py ne_110m_land.geojson /tmp/land.png src/worldland.inc
import json, sys
from PIL import Image, ImageDraw
W, H = 256, 128
g = json.load(open(sys.argv[1]))
im = Image.new('1', (W * 4, H * 4), 0)  # drawn 4x, then reduced: a cell is land if most of it is
d = ImageDraw.Draw(im)
def ring(coords):
    return [((lon + 180) / 360 * W * 4, (90 - lat) / 180 * H * 4) for lon, lat in coords]
for f in g['features']:
    geom = f['geometry']
    polys = [geom['coordinates']] if geom['type'] == 'Polygon' else geom['coordinates']
    for poly in polys:
        d.polygon(ring(poly[0]), fill=1)
        for hole in poly[1:]:
            d.polygon(ring(hole), fill=0)
small = im.convert('L').resize((W, H), Image.Resampling.BOX)
bits = bytearray(W * H // 8)
for y in range(H):
    for x in range(W):
        if small.getpixel((x, y)) >= 128:
            i = y * W + x
            bits[i // 8] |= 1 << (i % 8)
small.point(lambda v: 255 if v >= 128 else 0).save(sys.argv[2])
out = ['// SPDX-License-Identifier: GPL-3.0-or-later',
       '// Land and sea, 256 x 128 cells (equirectangular: x from 180 W, y from 90 N),',
       '// one bit a cell, low bit first. Rasterized from Natural Earth 1:110m land',
       '// (public domain, naturalearthdata.com) by tools/worldland.py.',
       'static const unsigned char kWorldLand[] = {']
for i in range(0, len(bits), 24):
    out.append('    ' + ', '.join('0x%02x' % b for b in bits[i:i + 24]) + ',')
out.append('};')
open(sys.argv[3], 'w').write('\n'.join(out) + '\n')
print(sum(bin(b).count('1') for b in bits), 'land cells of', W * H)
