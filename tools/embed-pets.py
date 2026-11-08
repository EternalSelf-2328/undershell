#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Embeds the pets' atlas PNGs (art/pets/*.png, art/pets/chibi/*.png) as
byte arrays for src/petart.cpp: kPetAtlas_<name> and kPetChibi_<name>.
Run by meson at build time; usage: embed-pets.py OUTPUT PNG...
"""
import pathlib
import sys

out = pathlib.Path(sys.argv[1])
parts = ["// Generated from art/pets by tools/embed-pets.py at build time.\n"]
for arg in sys.argv[2:]:
    p = pathlib.Path(arg)
    prefix = "kPetChibi_" if p.parent.name == "chibi" else "kPetAtlas_"
    data = p.read_bytes()
    parts.append(f"static constexpr unsigned char {prefix}{p.stem}[] = {{\n")
    for i in range(0, len(data), 24):
        parts.append(",".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",\n")
    parts.append("};\n")
out.write_text("".join(parts))
