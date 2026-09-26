# undershell

Desktop widgets that live **under** your shell and **apart** from it: a
standalone Wayland client in C++ (GLES 3) that doesn't depend on any shell or
dotfiles. It integrates with Noctalia (palette, `wallpaper_depth` depth) and
runs on Umbriel/niri/Hyprland. Its first widgets are ports of
[Ryoku](https://github.com/ryoku-dev/ryoku)'s.

- **Same layer as Noctalia's widgets** (layer-shell *bottom*): above the
  wallpaper, below the windows, and click-through.
- **Depth (`wallpaper_depth`)**: it reads the same mask the plugin generates
  (`sha256(wallpaper)-…-t<threshold>-f<feather>.png`) and applies it with
  Noctalia's own sampling math, so the widgets pass behind the wallpaper
  subject exactly like the native ones.
- **Noctalia colors**: the palette comes from `noctalia theme <wallpaper>
  --scheme <your scheme>` (the same generator the shell uses) and is
  recomputed when you change wallpaper or theme.
- **Lightweight**: PipeWire capture inside the same process (no cava),
  FFT + cava-style processing (adapted from Noctalia, MIT) and the whole
  spectrum drawn in **a single GPU pass** (Ryoku's shader). With music it
  uses ~3% CPU on an HD 520; with no audio it sleeps.

## Visualizer: 12 looks

`bars`, `split`, `dots`, `segments`, `wave`, `ribbon`, `curtain`, `line`,
`frame` (a ring around the whole screen), `radial`, `orb`, `spiral`: Ryoku's
`spectrum.frag` ported to GLSL ES 3.00, with glow, reflection, falling peaks
and Ryoku's motion (`Motion.qml`).

## Clock: 12 faces

`digital`, `minimal`, `analog`, `flip` (cards that flip, colon that
"breathes"), `rings`, `bighour` (outlined month and seconds), `metal` (with
the weather from Noctalia's cache), `goodnight` (the day in the "kana" alphabet),
`grand` (Fraunces serif), `column`, `outline` (hollow numerals) and `banner`
(with a halo). Dates: `inline`, `badge`, `stacked` or `none`. Translated from
Ryoku's QML with its same measurements; the design fits the widget box.
Day and month names follow the system language (or `language = "en" | "es"`).
It redraws only when the second (or minute) changes; `flip` animates at 30 fps.

## Now playing (card)

Ryoku's music card: the cover (downloaded and decoded in the background),
the album color on the plate, the progress bar, the pulse and the play
button (Ryoku's `accentOf`), synced lyrics from **LRCLIB** (cached in
`~/.cache/undershell/lyrics`) with the sung line lit and its neighbors
fading, or a live spectrum when there are no lyrics; wavy seek bar
(**click to jump**), previous / play-pause / next and a button that opens your
app (`music_app`). The player is the one from Noctalia's MPRIS aggregator
(`dev.noctalia.Mpris`, via sd-bus), with instant updates from signals.
Options: `plate` (cover, glass, none), `show_lyrics`, `viz` (bars, wave),
`accent_source` (album, theme), `music_app`, `fps`.

Widgets are click-through except on their buttons, so the desktop
keeps working normally around them.

## Usage

```sh
undershell                  # start (one instance only)
undershell msg edit         # editor on/off
undershell msg demo         # synthetic spectrum for previewing looks
undershell msg status       # widgets, fps, depth mask, audio
undershell msg add clock flip  # add a widget to the config: visualizer, clock, now_playing
undershell msg remove clock    # remove it
undershell msg select clock    # select it in the editor (scripts)
undershell msg gallery         # open the editor with the gallery
undershell msg reload
undershell msg quit
undershell --snapshot DIR   # render the 12 looks to PNG (no window needed)
```

**Editor** (`msg edit`, e.g. bound to `"Mod+Ctrl+D" = "spawn:/home/USER/.local/bin/undershell msg edit"`
in Umbriel; use the absolute path, since Umbriel's PATH doesn't include `~/.local/bin`):

| Action | How |
|---|---|
| Select | click (empty space: deselect) · **Tab** / Shift+Tab |
| Move / resize | drag · bottom-right corner |
| Magnet | snaps to the screen center, the edges and other widgets (guide lines); **Shift** = free |
| Fine-tune | **arrows** 1 px · Shift+arrows 16 px · **Alt**+arrows = size (with key repeat) |
| Change look | **wheel** over the widget (visualizer style or clock face) |
| Options | the **inspector** appears next to the selected widget: lists ‹ ›, switches, sliders (drag or wheel) and theme color swatches; you see every change live |
| Add | the **＋** button (bottom right) opens the gallery: visualizer, clock, music |
| Duplicate / delete | **Ctrl+D** · **Del** (or the inspector buttons) |
| Undo / redo | **Ctrl+Z** · **Ctrl+Shift+Z** / Ctrl+Y (moves, sizes, looks, options, additions and deletions) |
| Exit | **Esc**, Enter or right-click |

While editing with no music, the visualizers move with the demo spectrum.
Positions snap to the grid (16 px) on release and are saved to the config,
without touching the rest of the file.

## Configuration

`~/.config/undershell/config.toml`: it's created with defaults the first
time and **applies live** on save. Each `[[widget]]` has `id`, `output`,
`x`, `y`, `width`, `height` and the visualizer options (style, bars,
thickness, reflection, grow, shape, color_mode theme/gradient/custom,
color/color2 as a Noctalia role or `#hex`, gain, smoothing, peaks, mirror,
idle_wave, spin, glow, fps, opacity, depth). You can have several widgets.

## Build and tests

```sh
meson setup build --prefix=$HOME/.local
ninja -C build && meson test -C build && meson install -C build
```

The tests don't need a compositor: `config` (loading and in-place editing
of the file), `editor` (magnet, limits, grid), `media` (LRC, album color,
position), `motion` (Ryoku's easing) and `render` (the 12 looks against
the images in `tests/golden/`, compilation of every shader, and text with the
bundled fonts, via headless EGL). If you change a look on purpose: `build/test_render --update`.

## Structure

| | |
|---|---|
| `src/app.*` | Wayland, EGL, loop, surfaces, IPC |
| `src/editor.cpp`, `snap.hpp` | the editor: selection, magnet, keyboard, undo/redo, labels |
| `src/inspector.cpp`, `schema.hpp` | inspector (options per type) and widget gallery |
| `src/widget.*` | the widget interface (`WidgetImpl`) and the type registry |
| `src/visualizer.*`, `motion.hpp`, `shaders/` | the visualizer (Ryoku) |
| `src/clock.*` | the clocks (Ryoku): faces as display lists |
| `src/canvas.*` | 2D canvas: rects, circles, segments, arcs, triangles, wave, images + text |
| `src/nowplaying.*` | the music card (Ryoku) |
| `src/media.*`, `src/jobs.*` | MPRIS (sd-bus), covers, album color, lyrics; background jobs |
| `src/text.*` | text: Pango → cached textures |
| `src/audio.*` | PipeWire capture + FFT |
| `src/noctalia.*`, `src/depth.*` | Noctalia palette and depth masks |
| `src/offscreen.*` | headless rendering (`--snapshot`, tests) |
| `data/fonts/` | Space Grotesk, Fraunces, Inter Display, JetBrains Mono (OFL) |

Dependencies (all present on CachyOS with Noctalia): wayland, wayland-protocols,
EGL/GLES 3, libpipewire-0.3, toml++, glib, cairo, pango, fontconfig, xkbcommon,
libsystemd (sd-bus), gdk-pixbuf, libcurl, nlohmann-json.

## Status

- [x] Phase 0: visualizer (12 looks), depth, Noctalia palette, editor, IPC
- [x] Phase 1: widget interface, text, fonts, automated tests
- [x] Phase 2: editor (guides, arrows, undo/redo, demo while editing, labels)
- [x] Phase 3: clocks (12 faces, 3 dates), 2D canvas, hollow text and halo
- [x] Phase 4: now-playing card (MPRIS via sd-bus, cover, album color, LRCLIB lyrics, controls)
- [x] Phase 5: inspector, gallery, duplicate/delete, full undo
- [ ] Phase 6: autostart, PKGBUILD, documentation

## License

GPL-3.0-or-later (the shader and the motion are derived from Ryoku, GPL-3.0).
It includes code adapted from Noctalia (MIT): the spectrum analysis and the
wallpaper sampling math for the mask.
