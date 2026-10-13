#!/usr/bin/env bash
# Builds the package of one distribution for a release, inside a fresh
# container of that distribution (as root): installs what the build needs,
# builds with the same scripts people use at home, and copies the package to
# <outdir> with the distribution in its name. GitHub Actions runs it for every
# tag (.github/workflows/release.yml); it runs the same under podman:
#
#   podman run --rm -v "$PWD":/src:ro -v /tmp/out:/out docker.io/library/debian:13 bash -c \
#     'apt-get update && apt-get install -y git && git clone /src /work && /work/packaging/build-release.sh debian-13 /out'
#
#   packaging/build-release.sh <debian-13|ubuntu-24.04|fedora-43|fedora-44|arch> <outdir>
set -euo pipefail

target="${1:?usage: build-release.sh <target> <outdir>}"
out="${2:?usage: build-release.sh <target> <outdir>}"
repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo"
mkdir -p "$out"
trust() { git config --global --add safe.directory "$repo"; }  # the checkout belongs to another user

case "$target" in
  debian-*|ubuntu-*)
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -q
    apt-get install -y -q --no-install-recommends git ca-certificates dpkg-dev
    trust
    packaging/debian/build-deb.sh
    deb="$(find build-deb -maxdepth 1 -name 'undershell_*.deb' | head -1)"
    # the same version for both: the distribution goes in the name
    base="$(basename "$deb" _amd64.deb)"
    cp "$deb" "$out/${base}_${target}_amd64.deb"
    ;;
  fedora-*)
    dnf -y -q install git rpm-build dnf-plugins-core
    trust
    packaging/fedora/build-rpm.sh
    find build-rpm/RPMS -name 'undershell-[0-9]*.rpm' -exec cp {} "$out/" \;
    ;;
  arch)
    pacman -Syu --noconfirm --needed git base-devel
    trust
    # makepkg will not run as root: the dependencies first, then a plain user
    # builds this checkout
    (source packaging/arch/PKGBUILD && pacman -S --noconfirm --needed "${depends[@]}" "${makedepends[@]}")
    id builder >/dev/null 2>&1 || useradd -m builder
    work="$(mktemp -d)"
    cp packaging/arch/PKGBUILD "$work/"
    chown -R builder "$work"
    chmod -R a+rX "$repo"
    sudo -u builder git config --global --add safe.directory '*'
    sudo -u builder env UNDERSHELL_REPO="$repo" bash -c "cd '$work' && makepkg --noconfirm"
    find "$work" -maxdepth 1 -name 'undershell-[0-9]*.pkg.tar.zst' -exec cp {} "$out/" \;
    ;;
  *)
    echo "unknown target: $target" >&2
    exit 2
    ;;
esac
echo ":: $target:"
ls -l "$out"
