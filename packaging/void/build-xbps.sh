#!/usr/bin/env bash
# Run in an official Void container matching the desired libc.
set -euo pipefail
export LC_ALL=C

out=${1:?usage: build-xbps.sh <outdir>}
repo="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
mkdir -p -- "$out"
out="$(realpath "$out")"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
mkdir "$work/source"
git -C "$repo" archive HEAD | tar -xf - -C "$work/source"

xbps-install -Syu
xbps-install -y base-devel meson ninja python3 pkg-config \
  wayland-devel wayland-protocols libglvnd-devel pipewire-devel tomlplusplus-devel \
  libglib-devel cairo-devel pango-devel fontconfig-devel libxkbcommon-devel \
  elogind-devel gdk-pixbuf-devel libcurl-devel ffmpeg6-devel sqlite-devel \
  liblz4-devel json-c++

meson setup "$work/build" "$work/source" --prefix=/usr --buildtype=release \
  -Ddev_paths=false -Dvideo=enabled -Devolution=enabled -Dscenes=enabled \
  -Ddepth_model=disabled -Dscripts=disabled
meson compile -C "$work/build"
meson test -C "$work/build" --no-rebuild --print-errorlogs config docs editor media motion wallset
meson install -C "$work/build" --no-rebuild --destdir "$work/stage"
version="$("$work/stage/usr/bin/undershell" --version)"
version="${version#undershell }"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
arch="$(xbps-uhelper arch)"
pkgver="undershell-${version}_1"

# Resolve each DT_NEEDED entry to the installed Void package that provides it.
# Query the real library paths so this also works through linker symlinks.
for binary in "$work/stage/usr/bin/undershell" "$work/stage/usr/libexec/undershell-scene"; do
  readelf -d "$binary" | sed -n 's/.*(NEEDED).*\[\(.*\)\].*/\1/p' > "$work/needed"
  ldd "$binary" > "$work/linked"
  while IFS= read -r soname; do
    library="$(awk -v name="$soname" '$1 == name && $3 ~ /^\// {print $3}' "$work/linked")"
    if [[ -z "$library" ]]; then
      echo "Cannot resolve $soname in $binary" >&2
      exit 1
    fi
    owner="$(xbps-query -o "$(realpath "$library")")"
    xbps-uhelper getpkgname "${owner%%:*}"
  done < "$work/needed"
done > "$work/depends"
dependencies="$(sort -u "$work/depends" | sed 's/$/>=0/' | paste -sd ' ' -)"

cd "$work"
xbps-create -A "$arch" -n "$pkgver" -D "$dependencies" \
  -s "Wayland desktop widgets, audio visualizers and wallpaper effects" \
  -H "https://github.com/EternalSelf-2328/undershell" \
  -m "EternalSelf-2328 <71615506+EternalSelf-2328@users.noreply.github.com>" \
  -l "GPL-3.0-or-later" "$work/stage"
package="$pkgver.$arch.xbps"
xbps-rindex -a "$package"
xbps-install --repository="$work" -y undershell
test "$(undershell --version)" = "undershell $version"
mv -- "$package" "$out/"
