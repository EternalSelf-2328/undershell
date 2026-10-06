#!/usr/bin/env bash
# Builds undershell's RPM from this checkout, like `makepkg -si` on Arch:
# installs the build dependencies, packs the committed tree as the spec's
# source tarball, runs rpmbuild, and with -i installs the result.
#
#   packaging/fedora/build-rpm.sh       # build only
#   packaging/fedora/build-rpm.sh -i    # build and install
set -euo pipefail

install=0
[ "${1:-}" = "-i" ] && install=1

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
spec="$here/undershell.spec"
sudo_=sudo
[ "$(id -u)" -eq 0 ] && sudo_=""

for tool in git rpmbuild; do
  command -v "$tool" >/dev/null || { echo "missing $tool: sudo dnf install git rpm-build dnf-plugins-core" >&2; exit 1; }
done

echo ":: installing build dependencies"
$sudo_ dnf -y builddep "$spec"

version="$(rpmspec -q --qf '%{version}\n' "$spec" | head -1)"
top="$repo/build-rpm"
rm -rf "$top"
mkdir -p "$top"/{SOURCES,SPECS}
if [ -n "$(git -C "$repo" status --porcelain --untracked-files=no)" ]; then
  echo ":: note: uncommitted changes are not included (the package is built from HEAD)"
fi
git -C "$repo" archive --format=tar.gz --prefix="undershell-$version/" HEAD \
  -o "$top/SOURCES/undershell-$version.tar.gz"
cp "$spec" "$top/SPECS/"

echo ":: building undershell $version"
rpmbuild -ba --define "_topdir $top" "$top/SPECS/undershell.spec"

rpm="$(find "$top/RPMS" -name "undershell-$version-*.rpm" ! -name '*debug*' | head -1)"
echo ":: built $rpm"
if [ "$install" -eq 1 ]; then
  $sudo_ dnf -y install "$rpm"
  echo ":: installed; start it with: systemctl --user enable --now undershell"
fi
