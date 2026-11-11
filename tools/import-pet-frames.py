#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turns painted sprite sheets into the pets' frame atlases.

  tools/import-pet-frames.py SHEET.png... [--out art/pets/frames] [--preview DIR]

A sheet is 1536x1024, transparent, 6x4 cells of 256 px, one frame per cell
in the order of kFrameCells (src/petframes.hpp), facing right, the ground 24 px
above each cell's bottom (y = 232). Its name is <species id>-<style>.png
(style: chibi or pixel).

Each cell becomes a 128 px frame:
  - alpha made binary, specks removed (a painted sheet has stray pixels);
  - every 2x2 block becomes one art pixel (the sheets are painted on that grid);
  - the frames that stand are put exactly on the ground row (116), and every
    frame is centred by the middle of its upper body (not its legs, which
    swing), so a walk does not slide or jitter.
The atlas is <out>/<style>/<id>.png: 768x512, 6x4 cells of 128 px.
"""
import argparse
import pathlib
import sys

import numpy as np
from PIL import Image

COLS, ROWS, CELL = 6, 4, 256
OUT_CELL = CELL // 2
GROUND = 232 // 2  # the ground row in a 128 px frame
# painted off the ground (0-based): jump rise, jump fall, held
AIRBORNE = {13, 14, 20}
ACTS = {21, 22, 23}


def specks(alpha: np.ndarray, keep: int = 24) -> np.ndarray:
    """Drops opaque islands smaller than `keep` pixels (4-connected)."""
    h, w = alpha.shape
    seen = np.zeros_like(alpha, dtype=bool)
    out = alpha.copy()
    for y in range(h):
        for x in range(w):
            if not alpha[y, x] or seen[y, x]:
                continue
            stack, island = [(y, x)], []
            seen[y, x] = True
            while stack:
                cy, cx = stack.pop()
                island.append((cy, cx))
                for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                    if 0 <= ny < h and 0 <= nx < w and alpha[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
            if len(island) < keep:
                for cy, cx in island:
                    out[cy, cx] = False
    return out


def halve(rgba: np.ndarray) -> np.ndarray:
    """Each 2x2 block to one pixel: opaque if most of it is, coloured by its
    opaque pixel closest to the block's mean (a real painted colour, never a blend)."""
    h, w = rgba.shape[0] // 2, rgba.shape[1] // 2
    blocks = rgba[:h * 2, :w * 2].reshape(h, 2, w, 2, 4).transpose(0, 2, 1, 3, 4).reshape(h, w, 4, 4)
    alpha = blocks[..., 3] > 0
    count = alpha.sum(axis=2)
    out = np.zeros((h, w, 4), np.uint8)
    rgb = blocks[..., :3].astype(np.float32)
    mean = (rgb * alpha[..., None]).sum(axis=2) / np.maximum(count, 1)[..., None]
    dist = ((rgb - mean[:, :, None, :]) ** 2).sum(axis=3)
    dist[~alpha] = 1e9
    pick = dist.argmin(axis=2)
    chosen = np.take_along_axis(blocks, pick[..., None, None], axis=2)[:, :, 0, :]
    solid = count >= 2
    out[solid] = chosen[solid]
    out[solid, 3] = 255
    return out


def frames_of(sheet: Image.Image) -> list[np.ndarray]:
    if sheet.size != (COLS * CELL, ROWS * CELL):
        raise SystemExit(f'size {sheet.size}: expected {(COLS * CELL, ROWS * CELL)}')
    px = np.asarray(sheet.convert('RGBA')).copy()
    frames = []
    for i in range(COLS * ROWS):
        x0, y0 = (i % COLS) * CELL, (i // COLS) * CELL
        cell = px[y0:y0 + CELL, x0:x0 + CELL].copy()
        a = specks(cell[..., 3] >= 128)
        cell[..., 3] = np.where(a, 255, 0)
        cell[~a] = 0
        frames.append(halve(cell))
    return frames


def bbox(f: np.ndarray):
    ys, xs = np.nonzero(f[..., 3])
    if len(ys) == 0:
        return None
    return xs.min(), ys.min(), xs.max(), ys.max()


def body_centre(f: np.ndarray) -> float:
    """The middle of the figure's upper 60%: the head and torso, not the legs."""
    b = bbox(f)
    if b is None:
        return OUT_CELL / 2
    x0, y0, x1, y1 = b
    cut = y0 + int((y1 - y0) * 0.6)
    _, xs = np.nonzero(f[y0:cut + 1, :, 3])
    return float(xs.mean()) if len(xs) else (x0 + x1) / 2


def shift(f: np.ndarray, dx: int, dy: int) -> np.ndarray:
    out = np.zeros_like(f)
    h, w = f.shape[:2]
    sx0, sx1 = max(0, -dx), min(w, w - dx)
    sy0, sy1 = max(0, -dy), min(h, h - dy)
    out[sy0 + dy:sy1 + dy, sx0 + dx:sx1 + dx] = f[sy0:sy1, sx0:sx1]
    return out


def place(frames: list[np.ndarray]) -> tuple[list[np.ndarray], list[str]]:
    notes = []
    # one horizontal anchor for the whole sheet: the standing pose's body
    anchor = body_centre(frames[0])
    out = []
    for i, f in enumerate(frames):
        b = bbox(f)
        if b is None:
            notes.append(f'cell {i + 1}: empty')
            out.append(f)
            continue
        dx = int(round(OUT_CELL / 2 - anchor)) if i in ACTS or i in AIRBORNE else int(round(OUT_CELL / 2 - body_centre(f)))
        # every frame stands on the ground row: the program lifts a jumping or carried
        # companion itself. Only an action may leave the ground (a pounce's arc).
        dy = int(GROUND - b[3])
        if i in ACTS and abs(dy) > 4:
            dy = 0
        elif i not in AIRBORNE and abs(dy) > 4:
            notes.append(f'cell {i + 1}: feet {dy:+d} px off the ground, snapped')
        g = shift(f, dx, dy)
        nb = bbox(g)
        if nb and (nb[0] == 0 or nb[1] == 0 or nb[2] == OUT_CELL - 1):
            notes.append(f'cell {i + 1}: touches the frame edge after placing')
        out.append(g)
    return out, notes


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('sheets', nargs='+', type=pathlib.Path)
    ap.add_argument('--out', type=pathlib.Path, default=pathlib.Path('art/pets/frames'))
    ap.add_argument('--preview', type=pathlib.Path)
    args = ap.parse_args()
    status = 0
    for path in args.sheets:
        name = path.stem
        if '-' not in name:
            print(f'{path}: name it <species>-<style>.png', file=sys.stderr)
            status = 1
            continue
        sid, style = name.rsplit('-', 1)
        frames, notes = place(frames_of(Image.open(path)))
        atlas = np.zeros((ROWS * OUT_CELL, COLS * OUT_CELL, 4), np.uint8)
        for i, f in enumerate(frames):
            x0, y0 = (i % COLS) * OUT_CELL, (i // COLS) * OUT_CELL
            atlas[y0:y0 + OUT_CELL, x0:x0 + OUT_CELL] = f
        dest = args.out / style / f'{sid}.png'
        dest.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(atlas, 'RGBA').save(dest, optimize=True)
        print(f'{dest}: {len(frames)} frames' + ''.join(f'\n  - {n}' for n in notes))
        if args.preview:
            args.preview.mkdir(parents=True, exist_ok=True)
            bg = Image.new('RGBA', (atlas.shape[1], atlas.shape[0]), (38, 40, 48, 255))
            bg.alpha_composite(Image.fromarray(atlas, 'RGBA'))
            bg.resize((bg.width * 2, bg.height * 2), Image.Resampling.NEAREST).save(args.preview / f'{name}.png')
    return status


if __name__ == '__main__':
    sys.exit(main())
