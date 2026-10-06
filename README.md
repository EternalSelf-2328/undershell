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

## Requirements

- **A Wayland compositor with `wlr-layer-shell`**: Umbriel, niri, Hyprland,
  Sway, …
- **Optional:** [Noctalia](https://github.com/noctalia-dev/noctalia) for the
  palette and the depth effect (with its `wallpaper_depth` plugin); without it
  the widgets use their own colors and no depth. **PipeWire** for live audio
  in the visualizers.
- **Libraries:** wayland, EGL / GLES 3, libpipewire-0.3, toml++, glib, cairo,
  pango, fontconfig, xkbcommon, libsystemd (sd-bus), gdk-pixbuf, libcurl.
- **To build:** a C++20 compiler, meson, ninja, wayland-protocols,
  nlohmann-json, git.

On Arch and derivatives, all of it:

```sh
sudo pacman -S --needed base-devel git meson ninja wayland wayland-protocols libglvnd \
  libpipewire tomlplusplus glib2 cairo pango fontconfig libxkbcommon systemd-libs \
  gdk-pixbuf2 curl nlohmann-json
```

On Debian 13 and Ubuntu 24.04 or newer, the package script below installs
them for you (it needs GCC 13 or newer for C++20).

On Fedora, the package script below installs them for you; to build by hand
instead, after cloning the repository (see [Install](#install)), from inside it:

```sh
sudo dnf install dnf-plugins-core git
sudo dnf builddep packaging/fedora/undershell.spec
```

## Install

```sh
git clone https://github.com/EternalSelf-2328/undershell.git
cd undershell
```

**As a package (Arch and derivatives):**

```sh
cd packaging/arch && makepkg -si
systemctl --user enable --now undershell
```

**As a package (Fedora 43, 44):**

```sh
sudo dnf install git rpm-build dnf-plugins-core
packaging/fedora/build-rpm.sh -i
systemctl --user enable --now undershell
```

The script installs the build dependencies, builds the RPM from the checkout
(into `build-rpm/`) and installs it; without `-i` it only builds.

**As a package (Debian 13, Ubuntu 24.04 and newer):**

```sh
sudo apt install git dpkg-dev
packaging/debian/build-deb.sh -i
systemctl --user enable --now undershell
```

Same idea: build dependencies, a `.deb` in `build-deb/`, installed with `-i`.

**By hand:**

```sh
meson setup build --prefix=$HOME/.local
meson compile -C build
meson install -C build
systemctl --user daemon-reload && systemctl --user enable --now undershell
```

`meson install` puts the binary in `~/.local/bin` and the user service in
`~/.local/share/systemd/user/`, where `systemctl --user` finds it. The tests
are optional (see [Build and tests](#build-and-tests)).

**If the service doesn't start** ("A dependency job failed", "unmet condition
check ConditionEnvironment=WAYLAND_DISPLAY", or nothing appears at login):
your compositor doesn't run a systemd graphical session, so systemd doesn't
know the Wayland display. Add this to your compositor's autostart (Hyprland
`exec-once`, niri `spawn-at-startup`, Sway `exec`, …):

```sh
sh -c 'systemctl --user import-environment WAYLAND_DISPLAY && systemctl --user start undershell'
```

or skip the service and autostart `undershell` itself.

It runs as a **systemd user service** tied to the graphical session: it starts
with it, restarts itself if it crashes, and its logs go to the journal
(`journalctl --user -u undershell`). To stop it: `systemctl --user disable --now undershell`.

Every option is documented in [`docs/OPTIONS.md`](docs/OPTIONS.md) (generated
from the same table the inspector uses: `undershell --doc`). For the Noctalia
bar widget, see [Noctalia bar widget](#noctalia-bar-widget).

## Uninstall

First stop the service, whichever way you installed it:

```sh
systemctl --user disable --now undershell
```

Then remove it the way it was installed:

| Installed with | Remove with |
|---|---|
| the Arch package (`makepkg -si`) | `sudo pacman -R undershell` |
| the Fedora package (`build-rpm.sh -i`) | `sudo dnf remove undershell` |
| the Debian/Ubuntu package (`build-deb.sh -i`) | `sudo apt remove undershell` (`purge` instead of `remove` also drops its package record) |
| by hand (`meson install`) | `ninja -C build uninstall`, from the checkout you installed from |

If you installed by hand and no longer have that `build` folder, delete the
files yourself:

```sh
rm -rf ~/.local/bin/undershell ~/.local/share/undershell ~/.local/share/systemd/user/undershell.service \
       ~/.local/share/doc/undershell ~/.local/share/licenses/undershell
systemctl --user daemon-reload
```

Your layouts, saved profiles and depth corrections stay in
`~/.config/undershell`, and the lyrics cache in `~/.cache/undershell`. To
remove those too: `rm -rf ~/.config/undershell ~/.cache/undershell`. For the
Noctalia bar plugin, remove it from Noctalia's plugin settings (or, if you
linked it from a checkout, `noctalia msg plugins disable eternalself-2328/undershell`
and delete the link).

## Visualizer: 15 looks

`bars`, `split`, `dots`, `segments`, `wave`, `ribbon`, `curtain`, `line`,
`frame` (a ring around the whole screen), `radial`, `orb`, `spiral`: Ryoku's
`spectrum.frag` ported to GLSL ES 3.00, with glow, reflection, falling peaks
and Ryoku's motion (`Motion.qml`). Plus three of its own: `halo` (a glowing
ring), `vortex` and `fire` (see below).

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
undershell --snapshot DIR   # render the looks and clock faces to PNG (no window needed)
```

**Editor** (`msg edit`, e.g. bound to `"Mod+Ctrl+D" = "spawn:/home/USER/.local/bin/undershell msg edit"`
in Umbriel; use the absolute path, since Umbriel's PATH doesn't include `~/.local/bin`):

| Action | How |
|---|---|
| Toolbar | top center: **＋** add · undo · redo · magnet · grid · one chip per widget (selects it, even fullscreen looks like `frame`) · **?** shortcuts · **Done** |
| Select | click or its chip (empty space: deselect) · **Tab** / Shift+Tab |
| Move / resize | drag · **any corner** (the opposite one stays put, turned widgets too) |
| Magnet | snaps to the screen center, the edges and other widgets (guide lines); toggle it in the toolbar; **Shift** inverts it while held |
| Shape | The inspector's **Shape** section has a **Mode**: *None*; *Tilt* (lean back, lean sideways, skew and distance: `tilt_x`, `tilt_y`, `skew`, `perspective`); *Corners*, the widget's four corners as free points you drag onto the wallpaper; or *Mesh* (below). Saved as `warp = "none" \| "tilt" \| "pin" \| "mesh"`; a new shape starts from where the widget is drawn, and depth, clicks and the editor follow it. Placing points: a loupe magnifies the wallpaper under the one you drag, **Shift** moves it at a quarter speed, the arrows nudge the last one by 1 px (Shift 16 px), dragging inside moves them all, and the widget's resolution follows its shape |
| Mesh | Bends the widget to the shape of something in the wallpaper (a column, a flag, a rock, a box's corner): a grid of **3×3, 4×4 or 5×5** points you drag. **Between points**: *Smooth* curves through them, *Straight* keeps each cell flat and folds at the points. **Base shape** lays the grid as *Flat*, *Arc*, *Bulge*, *Flag*, *Cylinder* or *Wave* on the mesh's four corners, with a **Curvature** (−100…100; negative bends the other way); moving a point by hand makes it *Custom*. **Reset mesh** lays it flat again. Saved as `mesh_grid`, `mesh_between`, `mesh_preset`, `mesh_amount` and `mesh = [x0, y0, …]` (row by row from the top-left); `msg set <id> mesh_preset arc` works from scripts too |
| Other screens | A layout remembers the screen size it was made on (`space = [w, h]`; older blocks get the size of the screen they are first shown on). On a monitor of another size or scale every widget, pin and depth plane goes where the same part of the wallpaper is now (through the fill mode), so it stays on the rock or the portal it was placed on. The output's real logical size comes from xdg-output, so fractional scales (e.g. 1366×768 at 0.98) line up too |
| Layouts on any monitor | A saved profile loads onto the monitor it is loaded from (the editor's Profiles card), or the one chosen with **Load on** in the bar plugin, or onto **All** of them (`msg save-load <id> [output\|all]`); only that monitor's widgets are replaced, the others keep theirs, so the same profile can show on several screens; its `space` fits it to that screen's size through the wallpaper, so it also works across different resolutions. A block naming a monitor this machine does not have shows on the first one, so layouts can be shared between setups |
| Language | The editor speaks English or Spanish. By default it follows the system locale (`es_*` → Spanish, anything else → English); the **Language** buttons at the foot of the shortcuts card (`?`) pin it, saved as `[general] language = "system" \| "en" \| "es"`. Option values are shown translated (the config keeps the values themselves), and long labels shrink or end in “…” instead of running into the controls |
| Fold panels | The inspector and the depth brush panel have a chevron button in their title: it folds the panel to just its title (to see and paint the wallpaper behind it) and unfolds it again, in place. Panels still drag by their title while folded (`msg card fold inspector\|paint` does the same from a script) |
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
To use fonts you download, see [Fonts](#fonts). Untouched, a structure looks exactly as designed; changes are flat keys
in the widget (`goodnight_time_font = "Space Grotesk"`,
`goodnight_order = "rule_top,greeting,time,day,date,rule_bottom"`), so
they travel with wallpaper profiles and saved layouts. The other faces
stay as classics.

## Fonts

The five editable clock structures (`goodnight`, `column`, `flip`, `metal`,
`stacked`) can use any font installed on your system. The classic faces and
the music card keep the fonts of their design. Space Grotesk, Fraunces, Inter
Display, JetBrains Mono and Google Sans Flex come bundled, so they are always
there.

**Adding your own:**

1. Download the font as `.ttf` or `.otf` (variable fonts work too).
2. Copy the files to `~/.local/share/fonts/` (create the folder if needed;
   subfolders are fine). Fonts installed system-wide, for example with your
   package manager, work as well.
3. In the editor, select the clock, open a part under **Elements** and click
   its **Font**. The list is read again each time it opens, so new fonts show
   up without restarting undershell or running `fc-cache`. Type to search;
   each family is shown in its own typeface.
4. The first entry, **Design · …**, goes back to the face's own font.

**In the config file**, a part's font is `<face>_<part>_font`, with the family
name as `fc-list : family` prints it:

| Face | Parts with a font |
|---|---|
| `goodnight` | `greeting`, `date`, `time` (the weekday is drawn in strokes; `goodnight_day_style = "font"` sets it in `goodnight_day_font`) |
| `column` | `hours`, `minutes`, `extra` (seconds and AM/PM) |
| `flip` | `digits`, `colon` |
| `metal` | `time`, `ampm`, `weekday`, `date`, `weather` |
| `stacked` | `day`, `date`, `time` |

```toml
[[widget]]
type = "clock"
face = "stacked"
stacked_day_font = "Bebas Neue"
stacked_time_font = "JetBrains Mono"
```

If a family name doesn't match an installed font, Pango falls back to a
similar one; check the exact name with `fc-list : family | grep -i <name>`.

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

`integrations/noctalia/undershell` is a Noctalia plugin
(`eternalself-2328/undershell`): a bar button
(click: panel, right click: toggle the editor) and a panel that shows the
wallpaper profile and depth state, and per widget: behind the scenery on/off,
depth plane and tilt (committed when a slider is released), and a pencil that
opens the editor on it. It talks to the daemon with `undershell msg`
(`msg json` is its state). With several monitors, saved profiles can be
loaded onto one of them or all. Install it from Noctalia's plugin store (once
it is published there), or from this checkout:

```sh
mkdir -p ~/.local/share/noctalia/plugins/eternalself-2328
ln -s "$PWD/integrations/noctalia/undershell" ~/.local/share/noctalia/plugins/eternalself-2328/undershell
noctalia msg plugins enable eternalself-2328/undershell
```

then add the **undershell** widget to a bar in Noctalia's settings.

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
meson compile -C build && meson test -C build
```

The tests don't need a compositor: `config` (loading and in-place editing
of the file, layouts across screen sizes, languages), `depth` (depth maps,
refinement, the brush), `docs` (`docs/OPTIONS.md` is up to date), `editor`
(magnet, limits, grid), `media` (LRC, album color, position), `motion`
(Ryoku's easing) and `render` (the looks against the images in
`tests/golden/`, every shader, glow edges, and text with the bundled fonts,
via headless EGL). The golden images pin the exact pixels of the machine they
were made on; another GPU, Mesa or Pango version draws slightly differently,
so there a mismatch is only reported. `UNDERSHELL_GOLDEN=strict meson test -C build`
makes it fail (for development); if you change a look on purpose:
`build/test_render --update`.

## Structure

| Files | What it does |
|---|---|
| `src/app.*` | Wayland, EGL, loop, surfaces, IPC (`msg …`) |
| `src/anchor.hpp` | layouts that follow the wallpaper across screen sizes |
| `src/i18n.*` | interface language (system, English, Spanish) |
| `src/saves.cpp`, `src/profiles.cpp` | saved profiles; a layout per wallpaper |
| `src/geom.hpp` | rotation, perspective and corner pins (homographies) |
| `src/editor.cpp`, `snap.hpp` | the editor: selection, magnet, keyboard, undo/redo, labels |
| `src/inspector.cpp`, `schema.hpp` | inspector (options per type) and widget gallery |
| `src/widget.*` | the widget interface (`WidgetImpl`) and the type registry |
| `src/visualizer.*`, `motion.hpp`, `shaders/`, `*_shader.inc` | the visualizer (Ryoku's looks, halo, vortex, fire) |
| `src/clock.*`, `src/clockparts.*` | the clocks (Ryoku): faces as display lists; editable structures |
| `src/m3shapes.*` | Material 3 shapes (from Sung) |
| `src/canvas.*` | 2D canvas: rects, circles, segments, arcs, triangles, wave, images + text |
| `src/nowplaying.*` | the music card (Ryoku) |
| `src/media.*`, `src/jobs.*` | MPRIS (sd-bus), covers, album color, lyrics; background jobs |
| `src/text.*` | text: Pango → cached textures |
| `src/audio.*` | PipeWire capture + FFT |
| `src/noctalia.*`, `src/depth.*`, `src/depthfield.*`, `src/depthpaint.cpp` | Noctalia palette, depth masks, per-widget depth planes, the depth brush |
| `src/wallkind.*` | still or moving wallpaper (depth is off for videos) |
| `src/offscreen.*` | headless rendering (`--snapshot`, tests) |
| `data/fonts/` | Space Grotesk, Fraunces, Inter Display, JetBrains Mono, Google Sans Flex (OFL) |
| `integrations/noctalia/undershell/` | the Noctalia bar plugin |
| `packaging/arch/` | PKGBUILD |

## Status

A personal project, shared as is: it works day to day on the setup it was
made on (Umbriel + Noctalia, Arch), but it is young and not every compositor
or setup has been tried. It was written with a lot of help from an AI
assistant (Claude); issues and pull requests are welcome.

## License

GPL-3.0-or-later. Credits and third-party licenses (Ryoku GPL-3.0, Noctalia
MIT, OFL fonts, LRCLIB) are in [`NOTICE.md`](NOTICE.md).
