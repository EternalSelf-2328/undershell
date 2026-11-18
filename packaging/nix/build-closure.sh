#!/usr/bin/env bash
# Export the prebuilt program and all runtime dependencies from a Nix store.
set -euo pipefail

out=${1:?usage: build-closure.sh <outdir>}
repo="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
mkdir -p -- "$out"
work="$(mktemp -d)"
cleanup() {
  # Imported Nix store directories are read-only, including in this test store.
  if [[ -d "$work/test-store/nix/store" ]]; then
    find "$work/test-store/nix/store" -type d -exec chmod u+w {} +
  fi
  rm -rf -- "$work"
}
trap cleanup EXIT
mkdir "$work/source"
git -C "$repo" archive HEAD | tar -xf - -C "$work/source"

# NixOS 26.05, fixed to the same nixpkgs revision for every build of this release.
nixpkgs=https://github.com/NixOS/nixpkgs/archive/f68407657ad294471eb4b2e65d193df26edf729f.tar.gz
package="$(nix-build "$work/source/packaging/nix" -I "nixpkgs=$nixpkgs" \
  --out-link "$work/result")"
version="$("$package/bin/undershell" --version)"
version="${version#undershell }"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
arch="$(uname -m)"
name="undershell-${version}-nixos-${arch}.closure.xz"

# Nix emits the requisite paths in dependency order for import.
nix-store --query --requisites "$package" > "$work/requisites"
mapfile -t closure < "$work/requisites"
nix-store --export "${closure[@]}" | xz -T2 -3 > "$work/$name"

# Import into an empty store to verify that the archive is self-contained.
xz -dc "$work/$name" | nix-store --store "$work/test-store" --import > "$work/imported"
grep -Fxq "$package" "$work/imported"
mv -- "$work/$name" "$out/"
