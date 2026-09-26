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

## Usage

```sh
undershell                  # start (one instance only)
undershell msg edit         # editor on/off
undershell msg demo         # synthetic spectrum for previewing looks
undershell msg status       # widgets, fps, depth mask, audio
undershell msg reload
undershell msg quit
undershell --snapshot DIR   # render the 12 looks to PNG (no window needed)
```

**Editor**: `msg edit` makes the widgets interactive. Drag to move, the
bottom-right corner to resize, 16 px grid; on release it saves `x/y/width/height`
to the config (it only touches those lines, and keeps your comments).
**Right-click** exits the editor. You can bind it to a key in Umbriel, e.g.
`"Mod+Shift+W" = "spawn:undershell msg edit"`.

## Configuration

`~/.config/undershell/config.toml`: it's created with defaults the first
time and **applies live** on save. Each `[[widget]]` has `id`, `output`,
`x`, `y`, `width`, `height` and the visualizer options (style, bars,
thickness, reflection, grow, shape, color_mode theme/gradient/custom,
color/color2 as a Noctalia role or `#hex`, gain, smoothing, peaks, mirror,
idle_wave, spin, glow, fps, opacity, depth). You can have several widgets.

## Build

```sh
meson setup build --prefix=$HOME/.local
ninja -C build && meson install -C build
```

Dependencies (all present on CachyOS with Noctalia): wayland, wayland-protocols,
EGL/GLES 3, libpipewire-0.3, toml++, glib, cairo.

## Status

- [x] Phase 1: base, visualizer (12 looks), depth, Noctalia palette, editor
- [ ] Phase 2: clocks (12 faces)
- [ ] Phase 3: now-playing card (MPRIS, cover, lyrics)

## License

GPL-3.0-or-later (the shader and the motion are derived from Ryoku, GPL-3.0).
It includes code adapted from Noctalia (MIT): the spectrum analysis and the
wallpaper sampling math for the mask.
