## What's new in 1.9.0

- **Native desktop scenes.** Compose animated layers in the Scenes editor,
  share scenes with their local media, and switch saved layouts or static
  wallpapers by time, workspace, power, media playback or focus sessions.
- **Focus timer and desktop widgets.** Start focus sessions from the CLI or
  Noctalia. The new widgets include stereo scopes and meters, a spectrogram,
  photo slideshows, watched text or JSON data, and three animated dioramas.
- **More shapes and wallpaper fills.** Choose among 39 silhouettes and sample
  a patch of a static wallpaper directly from the editor.

## Fixes

- Editing a multiline TOML value replaces the original value and preserves its
  trailing comment. Widget IDs written with single quotes work in scripts and
  the editor. Invalid edits leave the previous configuration intact.
- Automatic scenes refresh when a saved profile is overwritten or deleted.
  The CLI reports when a visible widget belongs to a temporary scene.
- Replacing a slideshow's current photo under the same name refreshes it.
  Watched text, photos and other resources resume updating when their
  directories are recreated.

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
