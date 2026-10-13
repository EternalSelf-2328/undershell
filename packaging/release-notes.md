## Install a prebuilt package

Download the one for your system below, then:

| System | Install |
|---|---|
| Arch Linux, CachyOS, EndeavourOS… | `sudo pacman -U undershell-*.pkg.tar.zst` |
| Fedora 43 / 44 | `sudo dnf install ./undershell-*.fc43.x86_64.rpm` (or `fc44`) |
| Debian 13 | `sudo apt install ./undershell_*_debian-13_amd64.deb` |
| Ubuntu 24.04 or newer | `sudo apt install ./undershell_*_ubuntu-24.04_amd64.deb` |

Then start it: `systemctl --user enable --now undershell`, and open the editor
with `undershell msg edit`.

- The Ubuntu package is built without `undershell-depth` (Ubuntu has no ONNX
  Runtime package): depth behind the wallpaper's subject then needs Noctalia's
  `wallpaper_depth` plugin.
- `SHA256SUMS` lists every file's checksum: `sha256sum -c SHA256SUMS --ignore-missing`.
- Packages are x86_64 only. Anything else: build it from source (see Install in
  the README); `./update.sh` keeps a source build up to date.
