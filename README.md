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
| Toolbar | top center: **＋** add · undo · redo · magnet · grid · one chip per widget (selects it, even fullscreen looks like `frame`) · **?** shortcuts · **Done** |
| Select | click or its chip (empty space: deselect) · **Tab** / Shift+Tab |
| Move / resize | drag · **any corner** (the opposite one stays put, turned widgets too) |
| Magnet | snaps to the screen center, the edges and other widgets (guide lines); toggle it in the toolbar; **Shift** inverts it while held |
| Perspective | **Perspective** in the inspector: *Tilt back* leans the top away, *Tilt sideways* the right side, *Skew* makes a parallelogram, *Perspective depth* sets how strong it looks (`tilt_x`, `tilt_y`, `skew`, `perspective`); depth, clicks and the corner grips follow the warped shape |
| Tilt | drag the **knob** above the selected widget (0.1° steps; **Shift** = ten times finer; the magnet catches multiples of 15° within 2°) · **Ctrl+←/→** 0.1°, Ctrl+Shift 1° · Shift-drag any slider to nudge it · *Rotation* in the inspector · `rotation = 20` in the config (degrees, clockwise) |
| Fine-tune | **arrows** 1 px · Shift+arrows 16 px · **Alt**+arrows = size (with key repeat) |
| Change look | **wheel** over the widget (visualizer style or clock face) |
| Options | the **inspector** appears next to the selected widget, in sections (Look, Colour, Music, Time & data, Placement) showing only the settings that do something for its current look; drag it by its title to move it out of the way (double-click the title to put it back); the depth brush panel moves the same way. It offers: lists ‹ ›, switches, sliders (drag or wheel) and theme color swatches; you see every change live |
| Add | the **＋** button in the toolbar opens the gallery: visualizer, clock, music |
| Duplicate / delete | **Ctrl+D** · **Del** (or the inspector buttons) |
| Undo / redo | **Ctrl+Z** · **Ctrl+Shift+Z** / Ctrl+Y (moves, sizes, looks, options, additions and deletions) |
| Exit | **Esc**, Enter or right-click |

While editing with no music, the visualizers move with the demo spectrum.
With the grid on (toolbar), positions snap to it (16 px) on release and are saved to the config,
without touching the rest of the file.

## Music card

**Cover shape** cuts the cover to a Material 3 shape (`cover_shape`: any of
the 18 names), or `cycle`: each song gets its own shape (the same song, the
same shape) and the cover morphs into it on Material's expressive spring,
overshooting a little. `rounded` keeps the rounded square.

**Lyrics style** `poster` sets the sung line as a poster in Google Sans Flex
(bundled): each word takes its own weight, roundness and slant, each row is
set on the font's width axis until it fills the column from edge to edge (a
short one is set larger instead), and the line eases into that shape as it
arrives. `plain` keeps the plain lines.

## With nothing playing

`idle` in `[general]` sets what every visualizer does as soon as the sound
stops (a third of a second of grace): `show`
(as is, the default), `hide` (a quick fade out; a hidden visualizer stops
drawing, so it costs nothing) or `demo` (it moves on a demo spectrum). Each
visualizer's **With no music** option (`idle = "auto" | "show" | "hide" |
"demo"`) follows the general one on `auto` or overrides it. Music brings
them back at once; while editing they are always shown.

## Halo

The `halo` look is one perfectly round ring of light. Its glow is built
like real bloom (a tight, a medium and a wide falloff summed as light, then
tone mapped, so the core turns white-hot instead of clipping):

- **Every song fills the range** — each signal is normalised to the song's
  own recent range (a few seconds), so trap, swing and a ballad all move;
  how loud the song really is still counts, so calm music stays calmer.
  The first second and a half of sound eases in.
- **Pulse** — the bass envelope, on a soft curve: the light and the glow's
  reach pump with the groove.
- **Breathing** — the size follows the energy relative to its recent level
  on a critically damped spring: it swells with the phrasing and settles
  smoothly, never jitters.
- **Beat flash** — kicks, found by spectral flux on the bass (how fast it
  *rises*, over its own mean plus 1.6 deviations, not how loud it is), flash the light
  for about a quarter second; strong ones send a soft **wave** of light out.
- **Tone** — the colour drifts slowly from the primary toward the secondary
  as the music gets brighter (more treble).
- **Aurora** (off by default) — a slow brightness drift along the ring; the
  shape stays round.

**Shape** turns the ring into any of Material 3's 18 shapes (cookies,
flower, clovers, bursts, sunny, puffy diamond…), its line and glow kept even
along every curve.

Options: Shape, Size, Line width, Glow reach, Glow strength, Pulse, Breathing, Beat flash,
Beat waves, Aurora; *Smoothing* sets how soft the breathing is, *Gain* the
sensitivity, *Colour* the colours (theme: primary → secondary).

## Vortex

The `vortex` look takes the halo's ring as an event horizon and winds arms
of gaseous light around it in a logarithmic spiral, streaming inward like
water down a drain; the centre stays empty (put it around something on your
wallpaper and let depth keep that in front). It shares the halo's music —
pulse, breathing, beat flash and waves, all normalised per song — and adds
spin: the energy speeds it up on a spring and each kick gives a burst that
brakes by itself. **Mode**: `inward` (arms outside the ring, drawn in),
`outward` (outside, pouring out — a white hole), `inside` (a tunnel inside
the ring, sinking to the centre; only the halo's glow outside) and
`inside_out` (the tunnel welling up from the centre). **Ring** dims the
horizon ring down to 0 (arms only; their inner end then dissolves softly).
Options: Mode, Ring, Horizon size, Reach, Arms, Twist, Turbulence,
Spin speed, Clockwise, Horizon line, Glow strength, and the halo's music
options. Colour 1 lights the horizon, Colour 2 the outer arms.

## Fire

The `fire` look is a fire rising from the bottom of its box: flames torn
into tongues by noise that erodes the silhouette more as it rises, with a
textured, hot core and sparks that climb out of it and die. The music feeds
it like the halo (per-song normalisation): the energy sets the height, the
**spectrum** shapes it across the width (bass in the middle), the bass pulse
stokes the core, a kick throws a **flare** and a burst of sparks, and the
flames rise faster when the music is busy. Options: Shape (`bonfire`, a
cone; `wall`, full width), Colours (`fire`, a black-body ramp; `theme`,
Colour 1 for the core and Colour 2 for the tongues), Height, Turbulence,
Spectrum shape, Sparks, Speed, Pulse, Breathing, Beat flare.

## Clock structures

Five clock faces are **editable structures**: `goodnight` (the card),
`column`, `flip`, `metal` and `stacked` (the Modern Clock layout: a big
weekday, the date, the time). Select the clock in the editor: the
inspector's **Elements** section lists its parts — open one to set its
**font** (every installed family, each shown in itself; type to search),
size, weight, spacing, capitals and colour; the arrows reorder parts and
the switch hides one (flip keeps its layout). Each structure also has
options of its own: the card's weekday drawn in strokes or set in a font,
the column's line overlap and alignment, flip's corners and pulsing
separator, metal's separator, stacked's alignment, gap and date format.
Fonts you download: put the `.ttf`/`.otf` files in `~/.local/share/fonts`
(any installed font works too); the picker notices new files the next time
it opens, no restart needed. Untouched, a structure looks exactly as designed; changes are flat keys
in the widget (`goodnight_time_font = "Space Grotesk"`,
`goodnight_order = "rule_top,greeting,time,day,date,rule_bottom"`), so
they travel with wallpaper profiles and saved layouts. The other faces
stay as classics.

## Depth planes

With Noctalia's `wallpaper_depth` plugin, widgets pass behind the scenery.
By default every widget uses the plugin's mask (its threshold). Give a
widget its own plane with **Plano prof.** in the inspector (or
`depth_level = 1..100`): undershell reads the depth map the plugin cached,
refines it against the wallpaper exactly as the plugin does, and cuts each
widget at its own level on the GPU, live. Higher is nearer: at 95 almost
nothing covers the widget, at 5 almost everything does. `0` goes back to the
plugin's mask. Where widgets overlap, the nearer one is drawn on top
(a widget with depth off counts as nearest); **Layer** in the inspector
(`layer = -10..10`, higher in front) overrides that, e.g. for two widgets
on the same plane. The model never runs twice; refining takes under a second
per wallpaper, off the main thread.

### Moving wallpapers

A depth mask belongs to one still picture. When a video or a Wallpaper
Engine scene is on screen (undershell asks skwd-wall with `skwd-helm
current`, and also treats a video file set as the wallpaper as moving),
depth turns off for every widget and its controls lock — the inspector
rows, the depth brush, `msg set … depth`, and the Noctalia panel — until a
still wallpaper returns; the widgets' settings are kept. skwd's hooks can
call `undershell msg wallpaper-check` after each change for an instant
update.

### Fixing the depth by hand

The depth model guesses: an object may come out as a ramp (so a plane cuts
it in two), and the background next to an object often gets a halo of
false nearness. The **brush** button in the editor's toolbar opens the
depth brush: the wallpaper is tinted where it would cover the selected
widget (or, with none selected, the plugin's plane), and you correct it.

**Tools** (how you pick an area) — none needs a steady hand except the brush:

- **Brush** — paint; with *Keep to edges* on it only takes what looks like
  the spot under its centre. Shift+wheel sizes it.
- **Wand** — one click takes a whole object: the region that continues
  smoothly in tone and depth, filled solid. Shift+wheel sets the tolerance.
- **Lasso** — click around an object (no dragging); click the first point,
  double-click or press Enter to close; Backspace drops the last point, Esc
  cancels. With *Keep to edges* the outline settles onto nearby edges.
- **Hand** — drag to move the zoomed view (the middle button and the arrow
  keys do it with any tool).

**Actions** (what happens there): **To front**, **To back**, **Match** (the
depth where you start), **Erase** (corrections), **Smooth** (softens
jagged or abrupt depth).

**Zoom**: the wheel zooms about the pointer up to 8× (also + / − / 0 and the
panel's buttons); zoomed, the editor shows the wallpaper magnified and the
brush gets finer by the same factor. **Undo** / Ctrl+Z steps back one
stroke or selection; **Clear all** asks twice; right click or Esc leaves.

Corrections are kept per wallpaper in `~/.config/undershell/depth-edits/`
and apply to every widget, including those on the plugin's plane.

## A layout per wallpaper

undershell remembers where your widgets were for each wallpaper. Arrange
them for one wallpaper; when the wallpaper changes (Noctalia's picker,
skwd-wall, `noctalia msg wallpaper-set`, a script) the layout on screen is
filed under the old wallpaper and the new one's comes back: widgets, their
number, positions, tilt, looks and options. A wallpaper seen for the first
time keeps the current layout, so you start from it.

- `config.toml` always holds the layout on screen; the others wait in
  `~/.config/undershell/profiles/<sha256 of the image>.toml` (renaming or
  moving a wallpaper keeps its layout).
- Only the `[[widget]]` blocks move; `[general]` is shared.
- `undershell msg status` shows the current profile; `profiles = false` in
  `[general]` turns this off.

### Saved profiles

Besides that automatic memory, you can keep named layouts, like save slots
in a game: the **floppy** button in the editor's toolbar opens a panel with
every saved profile (a thumbnail, name, date, wallpaper and widget count).
**Save what is on screen** creates one and lets you type its name (Enter
keeps it); each row can be **loaded** (one undo step: Ctrl+Z brings the
previous layout back), **overwritten**, **renamed** or **deleted** (two
clicks). Loading a profile also makes it the current wallpaper's layout.
They live in `~/.config/undershell/saves/`, and the Noctalia panel lists
them too. From a terminal: `undershell msg saves | save [name] |
save-load <id> | save-overwrite <id> | save-rename <id> <name> |
save-delete <id>`.

## Noctalia bar widget

`integrations/noctalia/undershell` is a Noctalia plugin: a bar button
(click: panel, right click: toggle the editor) and a panel that shows the
wallpaper profile and depth state, and per widget: behind the scenery on/off,
depth plane and tilt (committed when a slider is released), and a pencil that
opens the editor on it. It talks to the daemon with `undershell msg`
(`msg json` is its state). To use it from a path source:

```sh
ln -s "$PWD/integrations/noctalia/undershell" ~/.local/share/noctalia/plugins/<your-source>/undershell
noctalia msg plugins enable <your-source>/undershell
```

then add the **undershell** widget to a bar in Noctalia's settings.

## Configuration

`~/.config/undershell/config.toml`: it's created with defaults the first
time and **applies live** on save. Each `[[widget]]` has `id`, `output`,
`x`, `y`, `width`, `height` and the visualizer options (style, bars,
thickness, reflection, grow, shape, color_mode theme/gradient/custom,
color/color2 as a Noctalia role or `#hex`, gain, smoothing, peaks, mirror,
idle_wave, spin, glow, fps, opacity, depth). You can have several widgets.

## Install

**As a package (Arch / CachyOS):**

```sh
cd packaging/arch && makepkg -si
systemctl --user enable --now undershell
```

**By hand:**

```sh
meson setup build --prefix=$HOME/.local
ninja -C build && meson test -C build && meson install -C build
systemctl --user daemon-reload && systemctl --user enable --now undershell
```

It runs as a **systemd user service** tied to the graphical session: it starts
with it, restarts itself if it crashes, and its logs go to the journal
(`journalctl --user -u undershell`). To stop it: `systemctl --user disable --now undershell`.

Every option is documented in [`docs/OPTIONS.md`](docs/OPTIONS.md) (generated
from the same table the inspector uses: `undershell --doc`).

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
- [x] Phase 6: systemd service, PKGBUILD, generated option docs, credits

## License

GPL-3.0-or-later. Credits and third-party licenses (Ryoku GPL-3.0, Noctalia
MIT, OFL fonts, LRCLIB) are in [`NOTICE.md`](NOTICE.md).
