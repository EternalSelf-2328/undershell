#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Embeds the painted companions' frame atlases (art/pets/frames/<style>/<id>.png)
for src/petframes.cpp, with the assembler's .incbin: the bytes go straight into
the binary without a giant C array to compile. Run by meson on every build; the
output only changes (and recompiles) when an atlas is added, removed or changed.
usage: embed-pet-frames.py OUTPUT FRAMES_DIR
"""
import hashlib
import pathlib
import sys

out = pathlib.Path(sys.argv[1])
root = pathlib.Path(sys.argv[2]).resolve()
styles = {'chibi': 0, 'pixel': 1}
files = sorted(p for s in styles for p in (root / s).glob('*.png')) if root.exists() else []
lines = ['// Generated from art/pets/frames by tools/embed-pet-frames.py at build time.\n']
entries = []
for i, p in enumerate(files):
    sym = f'undershell_petframe_{i}'
    digest = hashlib.sha256(p.read_bytes()).hexdigest()[:16]
    path = str(p).replace('\\', '\\\\').replace('"', '\\"')
    lines.append(f'// {p.parent.name}/{p.name} {digest}\n')
    lines.append(f'extern "C" const unsigned char {sym}[], {sym}_end[];\n')
    lines.append(f'asm(".section .rodata\\n.balign 16\\n.global {sym}\\n{sym}:\\n.incbin \\"{path}\\"\\n'
                 f'.global {sym}_end\\n{sym}_end:\\n.byte 0\\n.previous\\n");\n')
    entries.append(f'    {{"{p.stem}", {styles[p.parent.name]}, {sym}, {sym}_end}},\n')
lines.append('constexpr Embedded kEmbedded[] = {\n' + ''.join(entries) + '    {"", 0, nullptr, nullptr},\n};\n')
text = ''.join(lines)
if not out.exists() or out.read_text() != text:
    out.write_text(text)
