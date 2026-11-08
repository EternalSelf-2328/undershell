# Pequeños guardianes — chibi companions

Fourteen additional adorable versions of the Actitud collection, created
for Undershell with image generation using the original project artwork
as a reference. Each design keeps its signature colours and accessories,
with a larger head, expressive eyes, rounded body and short limbs.

The original generated images (`source/idle.png`, `stride.png`, `rest.png`
and `blink.png`) come in the releases' `undershell-pet-sources.tar`, not in
the repository. The prepared PNGs here contain 128 × 128 pixel cells in a
four-column, four-row layout; the final two cells are empty.

| Row | IDs, from left to right |
|---|---|
| 1 | `long_chibi`, `qilin_chibi`, `fenghuang_chibi`, `cerberus_chibi` |
| 2 | `pegasus_chibi`, `griffin_chibi`, `kitsune_chibi`, `fenrir_chibi` |
| 3 | `wyvern_chibi`, `unicorn_chibi`, `sentinel_chibi`, `rover_chibi` |
| 4 | `star_mage_chibi`, `moon_ranger_chibi`, empty, empty |

Build `tools/pack-pets.cpp` as described in [the parent README](../README.md),
then run `/tmp/undershell-pack-pets chibi` from the repository root. This
regenerates these four prepared atlases without changing the serious
collection; the build embeds them. `src/petart.cpp` loads each collection
only when needed. The chibi blink is a separate drawing with naturally closed eyelids;
it is used only on the standing pose. The original eye coordinates remain
exclusive to the serious collection.

Each character has a standing, stepping and resting drawing. The runtime
provides the six existing behaviours through two-frame cycles and small
pixel shifts. Sitting and sleeping share the resting drawing. Jumping
and dancing reuse the step. All orientations support horizontal mirroring.

`build/test_pets --gallery build/pets` exports the actual runtime sprites,
two-frame six-row sheets and `bestiary-chibi.png`, as well as the serious
collection. Chibi variants have their own IDs; existing configurations
keep the selected original designs.

Artwork and tooling: GPL-3.0-or-later; see the project's `LICENSE` and
`NOTICE.md`. These assets introduce no third-party franchise characters.
