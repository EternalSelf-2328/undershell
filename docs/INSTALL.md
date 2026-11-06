# Install

Part of [undershell](../README.md)'s documentation.

## Requirements

- **A Wayland compositor with `wlr-layer-shell`**: Umbriel, niri, Hyprland,
  Sway, KDE Plasma (KWin), COSMIC, … **GNOME** has no layer-shell: there the
  bundled GNOME Shell extension runs undershell (see [GNOME](DESKTOP.md#gnome)).
- **Optional:** [Noctalia](https://github.com/noctalia-dev/noctalia) for the
  palette. On KDE Plasma, undershell reads the wallpaper and the colour scheme
  itself; anywhere else, name the wallpaper in `[general]` (see
  [Without Noctalia](DESKTOP.md#without-noctalia-kde-plasma-and-others)). **ONNX
  Runtime** (`libonnxruntime`) for depth. **PipeWire** for live audio in the
  visualizers.
- **Libraries:** wayland, EGL / GLES 3, libpipewire-0.3, toml++, glib, cairo,
  pango, fontconfig, xkbcommon, libsystemd (sd-bus), gdk-pixbuf, libcurl,
  and FFmpeg's libavformat, libavcodec, libswscale and libavutil for the TV
  look's videos (optional: built without them, `-Dvideo=disabled`, GIFs still
  play). On Fedora, `ffmpeg-free` plays GIF, VP9 and AV1; H.264 needs the
  `ffmpeg` from RPM Fusion.
- **To build:** a C++20 compiler, meson, ninja, wayland-protocols,
  nlohmann-json, git.

On Arch and derivatives, all of it:

```sh
sudo pacman -S --needed base-devel git meson ninja wayland wayland-protocols libglvnd \
  libpipewire tomlplusplus glib2 cairo pango fontconfig libxkbcommon systemd-libs \
  gdk-pixbuf2 curl nlohmann-json ffmpeg
```

On Debian 13 and Ubuntu 24.04 or newer, the package script below installs
them for you (it needs GCC 13 or newer for C++20).

On Fedora, the package script below installs them for you; to build by hand
instead, after cloning the repository (see [Install](#install)), from inside it:

```sh
sudo dnf install dnf-plugins-core git
sudo dnf builddep packaging/fedora/undershell.spec
```

## Install

**Prebuilt packages:** every [release](https://github.com/EternalSelf-2328/undershell/releases/latest)
has packages for Arch Linux (and derivatives), Fedora 43 and 44, Debian 13 and
Ubuntu 24.04, built by GitHub Actions from the tagged source. Download the
one for your system, then:

```sh
sudo pacman -U undershell-*.pkg.tar.zst                       # Arch, CachyOS, EndeavourOS…
sudo dnf install ./undershell-*.fc44.x86_64.rpm               # Fedora (fc43 or fc44)
sudo apt install ./undershell_*_debian-13_amd64.deb           # Debian 13
sudo apt install ./undershell_*_ubuntu-24.04_amd64.deb        # Ubuntu 24.04 and newer
systemctl --user enable --now undershell
```

To update, install the next release's package the same way. The Ubuntu
package comes without `undershell-depth` (Ubuntu has no ONNX Runtime
package), so there depth behind the wallpaper's subject needs Noctalia's
`wallpaper_depth`.

**From source** (any distribution; `./update.sh` then keeps it up to date):

```sh
git clone https://github.com/EternalSelf-2328/undershell.git
cd undershell
```

**As a package (Arch and derivatives):**

```sh
cd packaging/arch && makepkg -si
systemctl --user enable --now undershell
```

**As a package (Fedora 43, 44):**

```sh
sudo dnf install git rpm-build dnf-plugins-core
packaging/fedora/build-rpm.sh -i
systemctl --user enable --now undershell
```

The script installs the build dependencies, builds the RPM from the checkout
(into `build-rpm/`) and installs it; without `-i` it only builds.

**As a package (Debian 13, Ubuntu 24.04 and newer):**

```sh
sudo apt install git dpkg-dev
packaging/debian/build-deb.sh -i
systemctl --user enable --now undershell
```

Same idea: build dependencies, a `.deb` in `build-deb/`, installed with `-i`.

**By hand:**

```sh
meson setup build --prefix=$HOME/.local
meson compile -C build
meson install -C build
systemctl --user daemon-reload && systemctl --user enable --now undershell
```

`meson install` puts the binary in `~/.local/bin` and the user service in
`~/.local/share/systemd/user/`, where `systemctl --user` finds it. The tests
are optional (see [Build and tests](DEVELOPMENT.md#build-and-tests)).

**If the service doesn't start** ("A dependency job failed", "unmet condition
check ConditionEnvironment=WAYLAND_DISPLAY", or nothing appears at login):
your compositor doesn't run a systemd graphical session, so systemd doesn't
know the Wayland display. Add this to your compositor's autostart (Hyprland
`exec-once`, niri `spawn-at-startup`, Sway `exec`, …):

```sh
sh -c 'systemctl --user import-environment WAYLAND_DISPLAY && systemctl --user start undershell'
```

or skip the service and autostart `undershell` itself.

It runs as a **systemd user service** tied to the graphical session: it starts
with it, restarts itself if it crashes, and its logs go to the journal
(`journalctl --user -u undershell`). To stop it: `systemctl --user disable --now undershell`.

Every option is documented in [`docs/OPTIONS.md`](OPTIONS.md) (generated
from the same table the inspector uses: `undershell --doc`). For the Noctalia
bar widget, see [Noctalia bar widget](DESKTOP.md#noctalia-bar-widget).

## Update

From the folder you cloned, one command for every install method:

```sh
./update.sh
```

It finds how undershell was installed (the Arch package, the Fedora RPM, the
Debian/Ubuntu package, or by hand in `~/.local`), runs `git pull`, builds and
installs it again the same way, and restarts the service if it was running.
`./update.sh --dry-run` only says what it would do; `--rebuild` installs again
even when there are no new commits. Packages built from the checkout carry
the commit count in their version (`1.2.0-1.git146`, `1.2.0+git146`), so `dnf`
and `apt` treat each update as newer.

The Noctalia bar plugin updates from Noctalia's plugin store.

## Uninstall

First stop the service, whichever way you installed it:

```sh
systemctl --user disable --now undershell
```

Then remove it the way it was installed:

| Installed with | Remove with |
|---|---|
| the Arch package (`makepkg -si`) | `sudo pacman -R undershell` |
| the Fedora package (`build-rpm.sh -i`) | `sudo dnf remove undershell` |
| the Debian/Ubuntu package (`build-deb.sh -i`) | `sudo apt remove undershell` (`purge` instead of `remove` also drops its package record) |
| by hand (`meson install`) | `ninja -C build uninstall`, from the checkout you installed from |

If you installed by hand and no longer have that `build` folder, delete the
files yourself:

```sh
rm -rf ~/.local/bin/undershell ~/.local/share/undershell ~/.local/share/systemd/user/undershell.service \
       ~/.local/share/doc/undershell ~/.local/share/licenses/undershell
systemctl --user daemon-reload
```

Your layouts, saved profiles and depth corrections stay in
`~/.config/undershell`, and the lyrics cache in `~/.cache/undershell`. To
remove those too: `rm -rf ~/.config/undershell ~/.cache/undershell`. For the
Noctalia bar plugin, remove it from Noctalia's plugin settings (or, if you
linked it from a checkout, `noctalia msg plugins disable eternalself-2328/undershell`
and delete the link).
