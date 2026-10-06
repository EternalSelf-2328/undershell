#!/usr/bin/env bash
# Builds undershell's .deb from this checkout, like `makepkg -si` on Arch:
# installs the build dependencies, builds the package with debhelper, and
# with -i installs the result. Works on Debian 13 and Ubuntu 24.04 or newer.
#
#   packaging/debian/build-deb.sh       # build only
#   packaging/debian/build-deb.sh -i    # build and install
set -euo pipefail

install=0
[ "${1:-}" = "-i" ] && install=1

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
sudo_=sudo
[ "$(id -u)" -eq 0 ] && sudo_=""

for tool in git dpkg-buildpackage; do
  command -v "$tool" >/dev/null || { echo "missing $tool: sudo apt install git dpkg-dev" >&2; exit 1; }
done

version="$(dpkg-parsechangelog -l "$here/changelog" -S Version)"
top="$repo/build-deb"
src="$top/undershell-$version"
rm -rf "$top"
mkdir -p "$src"
if [ -n "$(git -C "$repo" status --porcelain --untracked-files=no)" ]; then
  echo ":: note: uncommitted changes are not included (the package is built from HEAD)"
fi
# the committed tree, with the packaging as its debian/ folder
git -C "$repo" archive HEAD | tar -x -C "$src"
cp -r "$here" "$src/debian"
rm -f "$src/debian/build-deb.sh"

echo ":: installing build dependencies"
$sudo_ apt-get install -y --no-install-recommends build-essential
$sudo_ apt-get build-dep -y "$src"

echo ":: building undershell $version"
(cd "$src" && dpkg-buildpackage -us -uc -b)

deb="$(find "$top" -maxdepth 1 -name "undershell_${version}_*.deb" | head -1)"
echo ":: built $deb"
if [ "$install" -eq 1 ]; then
  $sudo_ apt-get install -y "$deb"
  echo ":: installed; start it with: systemctl --user enable --now undershell"
fi
