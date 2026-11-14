# Painted companions

`chibi/<species>.png` and `pixel/<species>.png` contain 24 painted frames each for the 36
species listed in [docs/PETS.md](../../../docs/PETS.md). Each atlas is
768 × 512: six columns and four rows of 128-pixel cells, facing right,
with the ground at row 116 inside each cell and binary transparency.
The runtime shows the drawings as painted, without bending them.

Cell order: idle, breathe, look, blink, sit, sit/look, six walk frames,
crouch, jump rise, jump fall, land, alert, happy, sleep, sleep/breathe,
carried, and three action frames (`src/petframes.hpp`).

The source sheets are 1536 × 1024 PNGs, made for undershell with image
generation. The selected 72 sheets are attached to releases as
`undershell-painted-pet-sources.tar.zst`, with a checksum manifest. To import a sheet named `<species>-chibi.png` (or
`<species>-pixel.png`):

```sh
python3 -I tools/import-pet-frames.py /path/to/sheets/*-chibi.png --preview build/pet-import-preview
python3 -I tests/test_pet_import.py
meson compile -C build
UNDERSHELL_GOLDEN=strict meson test -C build
build/test_pets --gallery build/pets
```

The importer requires Pillow and NumPy. It removes specks, chooses a real
painted colour from each 2 × 2 block, grounds each pose and centres the
upper body. A shared horizontal offset keeps long tails inside the frame.
The build embeds the PNGs with `.incbin` (`tools/embed-pet-frames.py`);
atlases are decoded on first use with Cairo, with no runtime downloads,
asset path, image-loader process or D-Bus connection. The decoder accepts
only the expected size and binary transparency; the runtime tests exercise
all 1728 frames, their mirroring, ground alignment and complete walk cycles.

Both styles are complete for the 36 species. If a future species has only
one style, the other style falls back to it. The classic
`*_classic` and `*_chibi` collections are separate and keep their artwork.

Artwork and tooling: GPL-3.0-or-later, like undershell.

The importer rejects any placement that would crop painted pixels. Actions
may float above the ground; poses below the ground are snapped up. Atlas
row 0 can hold artwork: composition adds eleven transparent rows above it.
Noisy alpha in generated source images is made binary during import; source
sheets remain archived alongside the exact prompts that produced them.
