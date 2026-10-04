#!/usr/bin/env bash
# Stages the Noctalia bar plugin for a pull request to
# https://github.com/noctalia-dev/community-plugins: copies
# integrations/noctalia/undershell into <checkout>/undershell, without the
# locales other than English (that repository takes translations/en.json
# only; the others come from Noctalia Translate).
#
#   packaging/noctalia-community.sh ~/src/community-plugins
set -euo pipefail

dest_repo="${1:?usage: $0 <community-plugins checkout>}"
src="$(cd "$(dirname "$0")/.." && pwd)/integrations/noctalia/undershell"
dest="$dest_repo/undershell"

[ -d "$dest_repo/.git" ] || { echo "not a git checkout: $dest_repo" >&2; exit 1; }
rm -rf "$dest"
mkdir -p "$dest/translations"
cp "$src"/{plugin.toml,bar.luau,panel.luau,README.md,LICENSE,thumbnail.webp} "$dest/"
cp "$src/translations/en.json" "$dest/translations/"
echo "staged $(find "$dest" -type f | wc -l) files in $dest"
