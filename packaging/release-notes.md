## What's fixed in 1.8.1

A stability and bug-fix release addressing over 40 verified issues found across the codebase, plus clock update reliability.

### Clock update reliability

- **Minute/second boundary lag:** `ClockWidget` and `{time}` text widgets now read the same high-resolution `CLOCK_REALTIME` source as `nextWakeup()` (with a 20 ms margin) instead of Linux `CLOCK_REALTIME_COARSE` (`std::time`), preventing clocks from redrawing the previous minute and sleeping for another 60 seconds.
- **Suspend/resume and NTP jumps:** the main loop detects wall-clock vs monotonic-clock jumps (> 1.5 s) and immediately re-arms scheduled widgets.
- **Withheld Wayland frame callbacks:** scheduled wall-clock widgets (desktop and screensaver clocks) cancel stale `wl_surface_frame` callbacks after 2 seconds when `wakeAt` is due, preventing clocks from freezing when a compositor withholds `frame.done` under occlusion, DPMS, or VFR.

### Memory safety and crash fixes

- **Use-after-free on locked mutexes:** fixed deterministic UAF in `ActivityWidget::tick()` and `WallpaperStore::get()` where `shared_ptr::reset()` destroyed the mutex while `std::lock_guard` still held it, and in `WallpaperFade::update()` where `trim()` could evict `m_cur` before reading it.
- **Buffer bounds:** fixed heap over-read in `Terminal::drawMatrix()` on vertical resize, empty `deque::back()` in `Terminal::stepCode()` when switching modes mid-line, heap overflow in `App::paintUndo()` for effect masks after wallpaper changes, and out-of-bounds reads in `App::fieldAt()` (`wandAt`/`lassoClose`) at right/bottom edges.
- **Uncaught exceptions on external data:** guarded `nlohmann::json` type mismatches in `WeatherService::parseForecast()` (`"hourly": null` / `"daily": null`) and `shells.cpp` (Caelestia, DMS, pywal, Ambxst, end-4), throwing `/sys` directory iterators and malformed `/proc/stat` lines in `sysmon.cpp`, unbounded `.npy` header allocations / `std::stoi` in `loadNpyF32()`, `fs::create_directories` in `profiles.cpp`, and exit-time teardown in `NotifyWatch`.
- **Calendar ICS parsing:** nested `VALARM` blocks no longer overwrite the parent `VEVENT`'s `SUMMARY` or `DURATION`, and extreme `INTERVAL`/`DURATION` values no longer overflow or dereference null `GDateTime*`.

### Editor, configuration and documentation

- **Multi-line `text` undo:** `Config::tomlText()` now escapes `\n`, `\r`, `\t`, and control characters, and `storeOption()` unescapes TOML strings so undoing multi-line text never writes invalid TOML into `config.toml`.
- **Profile switching safety:** `Config::switchProfile()` now checks file reads with `readFileChecked()` so transient read failures cannot wipe `config.toml` or saved profiles.
- **Editor interaction:** fixed `m_drag` getting permanently stuck when clicking a widget while renaming a layer, stopped `widgetAt()` from hitting hidden or locked selected widgets, fixed double `SpaceMap` inverse transforms on `pin`/`mesh` undo and `pin_corners`, offset `pin`/`mesh` points in `duplicateWidget()`, ensured `msg card color` populates `m_inspSchema`, and ensured `~App()` destroys active screensaver surfaces before EGL/Wayland teardown.
- **Inspector, i18n and docs:** fixed unreachable `lyrics_style` visibility condition when `show_lyrics = false`, added Spanish labels for clock `ink`/`accent` swatches, and restored missing `system`, `activity`, and `pet` headings in `docs/OPTIONS.md`.

### Graphics, scenes and resource leaks

- **Wallpaper Engine effect masks:** `SceneWidget::drawLayers()` now sets `lc.effectMasksUsed` so packed per-layer effect masks are actually applied instead of being ignored.
- **Shader NaNs:** fixed GLSL ES 3.00 undefined `smoothstep(edge0 >= edge1)` and `0/0` NaNs in `fx/droplets.inc`.
- **GL, QuickJS and FD leaks:** added `~TermScreen()` cleanup, called `VideoSource::releaseGl()` in `Tv` and `SceneWidget`, guarded `glDeleteTextures()` on shutdown in `ThumbCache` and `MediaService`, freed overwritten QuickJS script handles and cleared pending exceptions in `undershell-script`, applied EXIF orientation in `undershell-depth` to match `refineDepth()`, and closed the leaked `/dev/null` FD in `WindowFeed`.

### Companions, services and Noctalia plugin

- **Pets:** fixed default `Sprout` species missing hitboxes (`inputRects`) and speech bubbles, fixed rigged pet crouch animation skipping `m_life.step()`, fixed `std::clamp` UB on 1-character bubbles and mid-codepoint UTF-8 truncation, applied `body_color`/`mark_color` overrides on drawn/painted pets under `paint = "variant"`, and skipped `scope == "Device"` batteries.
- **Services and UI:** reset per-track metadata and `canToggle` cleanly in `MediaService::onPlayerReply()`, refreshed `colors_from = "cover"` immediately in `setPaletteWanted()`, replaced fragile regex JSON parsing in `skwdShowsMotion()`/`skwdPaths()` with `nlohmann::json`, fixed `Globe::moving()` for active kick rings when `globe_spin = 0`, used `g_utf8_strup` for accented calendar headings, fixed the unreachable `panel.no_widgets` empty state in the Noctalia panel plugin, and added missing `<cmath>` includes for GCC 16 builds.

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
