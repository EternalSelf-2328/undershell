## What's new in 1.8.0

### Painted companions

- **36 companions in two art styles:** chibi and side-view pixel art, with 24
  painted frames per species and style: **72 atlases and 1,728 frames**.
- Six-frame walking cycles, breathing, blinking, sleeping, jumping, alerts and
  species-specific actions. Select **Art → Chibi / Pixel art** in the pet's
  inspector. The classic collections remain available.
- Embedded artwork decodes directly with the existing Cairo dependency.
  Importing a sheet preserves long tails and rejects placements that crop
  painted pixels.

[Companion catalogue](https://github.com/EternalSelf-2328/undershell/blob/v1.8.0/docs/PETS.md)

### Mirrors and water

- Add **Reflection** under **Animated effects → Water** to mirror the picture
  above a configurable rectangle. Choose a still mirror or animated water,
  with wave size/speed, vertical stretch, soft edges and fading.
- Reflections now carry transparency and opacity, so they appear in empty
  parts of visualizers, text and image widgets.
- A still mirror does not request continuous frames.

[Reflection guide](https://github.com/EternalSelf-2328/undershell/blob/v1.8.0/docs/WALLPAPER.md#mirror-and-water-reflections)

### Fixes and controls

- **Frame visualizer:** the corner fans now reach the physical screen corners
  while retaining rounded inner tips and the mirrored spectrum.
- **Split:** the inspector offers horizontal/vertical orientation.
- **Curtain:** the inspector offers its four supported growth edges.
- Existing saved directions retain their equivalent appearance.

### Artwork sources

The optional `undershell-pet-sources.tar` contains the classic generated source
pictures. `undershell-painted-pet-sources.tar.zst` contains the 72 selected
painted source sheets and their checksums. These are for editing or repacking;
all runtime artwork is already embedded in the packages.

## Install a prebuilt package

Download the one for your system below, then:

| System | Install |
|---|---|
| Arch Linux, CachyOS, EndeavourOS… | `sudo pacman -U undershell-*.pkg.tar.zst` |
| Fedora 43 / 44 | `sudo dnf install ./undershell-*.fc43.x86_64.rpm` (or `fc44`) |
| Debian 13 | `sudo apt install ./undershell_*_debian-13_amd64.deb` |
| Ubuntu 24.04 | `sudo apt install ./undershell_*_ubuntu-24.04_amd64.deb` |
| NixOS (x86_64) | Import the `.closure.xz` archive and install it into a Nix profile. See [installation](https://github.com/EternalSelf-2328/undershell/blob/main/docs/INSTALL.md#nixos). |
| Void Linux (x86_64 glibc or musl) | Index the matching `.xbps` and install it with XBPS. See [installation](https://github.com/EternalSelf-2328/undershell/blob/main/docs/INSTALL.md#void-linux). |

Start Undershell in your Wayland session. For systemd package installs use
`systemctl --user enable --now undershell`. On NixOS and Void you can start
`undershell` from the compositor's autostart. Open the editor with
`undershell msg edit`.

- The Ubuntu and Void packages are built without `undershell-depth`.
  Generating new subject-depth maps requires a compatible `undershell-depth`
  helper and ONNX Runtime installation.
- Wallpaper Engine scenes work in every package; their scripts (clocks and dates
  on a scene's text layers) are supported by the Arch and NixOS packages.
- `SHA256SUMS` lists every file's checksum: `sha256sum -c SHA256SUMS --ignore-missing`.
- Packages are x86_64 only. Anything else: build it from source (see Install in
  the README); `./update.sh` keeps a source build up to date.
