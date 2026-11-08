# Actitud and Chibi — artwork sources

These fourteen companion designs were created for Undershell with image
generation, using the previous project designs as the starting reference.
The original generated pictures (`idle.png`, `stride.png` and `rest.png`,
about 2.5 MB each) are not in the repository: only the importer needs them,
and they are attached to the releases as `undershell-pet-sources.tar`
(unpack it at the repository root: it fills `art/pets/source/` and
`art/pets/chibi/source/`). They share a four-column, four-row layout; the
last two cells are empty. No franchise sprites were imported.

An additional chibi version of every design is in [`chibi/`](chibi/README.md),
with its own source atlases. The original serious artwork remains unchanged.

Cell order in either collection, left to right and top to bottom:

1. Long, Qilin, Fenghuang, Cerberus
2. Pegasus, Griffin, Kitsune, Fenrir
3. Wyvern, Unicorn, Bastion (`sentinel`), Copper (`rover`)
4. Luma (`star_mage`), Nox (`moon_ranger`), empty, empty

The design direction uses assertive poses, adult proportions, asymmetrical
silhouettes, expressive faces and a shared ivory/jade/gold/oxblood palette.
The original standing and resting poses are distinct drawings. Walking
alternates the standing and stride drawings; jumping and dancing reuse the
stride. Sitting and sleeping share the resting drawing. These are short,
two-frame animation cycles, not full frame-by-frame walking sequences.

## Rebuild the runtime atlas

From the repository root, with a C++20 compiler and gdk-pixbuf development
files available:

```sh
c++ -std=c++20 -O2 tools/pack-pets.cpp \
  $(pkg-config --cflags --libs gdk-pixbuf-2.0) -o /tmp/undershell-pack-pets
/tmp/undershell-pack-pets        # serious collection
/tmp/undershell-pack-pets chibi  # additional chibi collection
meson compile -C build
meson test -C build pets
build/test_pets --gallery build/pets
```

The importer divides each source into equal cells, rejects translucent
fringes and isolated speckles, fits each drawing onto a 128 × 128 grid with
clear margins, and aligns the feet to the same baseline. Resting drawings
use a smaller scale. Sampling uses nearest pixels and alpha is binary;
there is no smoothing. Output: the three 512 × 512 PNGs in this directory;
with the `chibi` argument, the four atlases (with the blink drawing) in
`art/pets/chibi/`. Those PNGs are the build's source: meson embeds them into
the executable (`tools/embed-pets.py`), so there is nothing else to keep in
sync.

`src/petart.cpp` decodes the embedded PNGs once and adds blinking, mirroring
and small movement/sleep/music variants. If a standing face moves, update
its blink coordinates there for Actitud, or the separate blink atlas for
Chibi. The executable needs no separate pet files.
The rendered gallery is an accurate preview of the runtime sprites.

Artwork and tooling: GPL-3.0-or-later, as provided in the repository's
`LICENSE`. See `NOTICE.md` and `docs/PETS.md` for provenance and character
licensing notes. The generated concepts do not include Hatsune Miku.
