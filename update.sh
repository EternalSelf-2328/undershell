#!/usr/bin/env bash
# Updates undershell the way it was installed: pulls this checkout, builds it
# again by the same route (Arch package, Fedora RPM, Debian/Ubuntu .deb, or a
# hand install in ~/.local) and restarts the service if it was running.
#
#   ./update.sh             update if there are new commits
#   ./update.sh --rebuild   build and install again even without new commits
#   ./update.sh --dry-run   only say what it would do
set -euo pipefail

rebuild=0 dry=0
for arg in "$@"; do
  case "$arg" in
    --rebuild) rebuild=1 ;;
    --dry-run) dry=1 ;;
    -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $arg (try --help)" >&2; exit 2 ;;
  esac
done

repo="$(cd "$(dirname "$0")" && pwd)"
cd "$repo"
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || { echo "not a git checkout: $repo" >&2; exit 1; }

# how it was installed: a package wins over a copy in ~/.local
if command -v pacman >/dev/null && pacman -Qq undershell >/dev/null 2>&1; then
  method=arch
elif command -v rpm >/dev/null && rpm -q undershell >/dev/null 2>&1; then
  method=fedora
elif command -v dpkg-query >/dev/null && dpkg-query -W -f='${Status}' undershell 2>/dev/null | grep -q "install ok installed"; then
  method=debian
elif [ -x "$HOME/.local/bin/undershell" ]; then
  method=hand
else
  echo "undershell is not installed yet: see Install in README.md" >&2
  exit 1
fi
case "$method" in
  arch) build="cd packaging/arch && makepkg -sif" ;;
  fedora) build="packaging/fedora/build-rpm.sh -i" ;;
  debian) build="packaging/debian/build-deb.sh -i" ;;
  hand) build="meson setup build --prefix=\$HOME/.local (first time) && meson compile -C build && meson install -C build" ;;
esac
echo ":: installed as: $method"

if [ "$dry" -eq 1 ]; then
  echo ":: would run: git pull --ff-only"
  echo ":: then, if there are new commits (or with --rebuild): $build"
  echo ":: and restart the service if it is running"
  exit 0
fi

before="$(git rev-parse HEAD)"
echo ":: git pull"
git pull --ff-only
after="$(git rev-parse HEAD)"
if [ "$before" = "$after" ] && [ "$rebuild" -eq 0 ]; then
  echo ":: already up to date (undershell $(git describe --tags --always 2>/dev/null))"
  exit 0
fi
[ "$before" != "$after" ] && git --no-pager log --oneline "$before..$after" | sed 's/^/   /'

echo ":: building and installing"
case "$method" in
  arch) (cd packaging/arch && makepkg -sif) ;;
  fedora) packaging/fedora/build-rpm.sh -i ;;
  debian) packaging/debian/build-deb.sh -i ;;
  hand)
    [ -d build ] || meson setup build --prefix="$HOME/.local"
    meson compile -C build
    meson install -C build
    ;;
esac

systemctl --user daemon-reload 2>/dev/null || true
if systemctl --user is-active --quiet undershell 2>/dev/null; then
  systemctl --user restart undershell
  echo ":: restarted the service"
fi
bin="$(command -v undershell || echo "$HOME/.local/bin/undershell")"
echo ":: done: $("$bin" --version 2>/dev/null || echo undershell)"
