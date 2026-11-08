# Development

Part of [undershell](../README.md)'s documentation.

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
| `src/pet.*`, `src/petart.*` | desktop companion behaviour, species catalogue and embedded 128 × 128 sprite artwork |
| `src/visualizer.*`, `motion.hpp`, `shaders/`, `*_shader.inc` | the visualizer (Ryoku's looks, halo, vortex, fire) |
| `src/clock.*`, `src/clockparts.*` | the clocks (Ryoku): faces as display lists; editable structures |
| `src/m3shapes.*` | Material 3 shapes (from Sung) |
| `src/canvas.*` | 2D canvas: rects, circles, segments, arcs, triangles, wave, images + text |
| `src/nowplaying.*` | the music card (Ryoku) |
| `src/media.*`, `src/jobs.*` | MPRIS (sd-bus), covers, album color, lyrics; background jobs |
| `src/text.*` | text: Pango → cached textures |
| `src/textfx.*`, `src/textwidget.*` | text with effects (fill, outline, shadow, glow, 3D, moving letters) and the text widget |
| `src/lyricswidget.*` | the lyrics widget: synced lines, layouts, transitions, karaoke |
| `src/calendar.*` | the calendar widget: month, week, today's page, agenda |
| `src/ics.*`, `src/calsources.*` | iCalendar events (repeats, time zones) and where they come from: .ics, khal, Evolution |
| `src/notes.*` | the notes widget: Markdown, tasks, typing on it, its watched file |
| `src/weather.*`, `src/weatherfeed.*` | the weather widget and its Open-Meteo feed (geocoding, forecast, disk cache) |
| `src/net.*` | HTTPS fetches (http/https only, size-limited) and the cache folder |
| `src/filter.*` | the wallpaper widget: the wallpaper drawn again through colour adjustments and the animated effects (one pass, plus a smaller one for the lights) |
| `src/scripthelper.cpp`, `src/scripthost.*` | `undershell-script` (scene scripts in quickjs-ng, confined) and the scene widget's side of it |
| `src/scene.*`, `src/wescene.*`, `src/scenehelper.cpp` | the scene widget (Wallpaper Engine scenes and videos), reading an unpacked scene, and `undershell-scene`, the confined helper that unpacks them |
| `src/particles.*`, `src/pt/*.inc` | particles over the wallpaper: the JSON format, the built-in systems, your own folder, the fixed-step simulation and the instanced sprites |
| `src/effectchain.*`, `src/fxwidget.*` | the effects of one widget ready to draw (options, music, programs, lights pass), and the wrapper that gives them to every widget type |
| `src/effects.*`, `src/fx/*.inc` | animated effects: the annotated format, the built-in effects, your own folder, joining them into one shader, their inspector rows |
| `src/shape.*` | the shape widget (one distance shader) |
| `src/image.*`, `src/video.*` | the image widget; decoding stills, GIFs and videos (FFmpeg, gdk-pixbuf) |
| `src/layers.cpp` | the Layers panel's model: order, groups, hiding, locking, moving several widgets |
| `src/audio.*` | PipeWire capture + FFT |
| `src/noctalia.*`, `src/depth.*`, `src/depthfield.*`, `src/depthpaint.cpp` | Noctalia palette, depth masks, per-widget depth planes, the depth brush |
| `src/plasma.*` | KDE Plasma: wallpapers per screen, fill mode, palette from the colour scheme |
| `src/gnome.*`, `integrations/gnome/` | GNOME: wallpaper and accent from gsettings; the GNOME Shell extension that puts the widget windows on the desktop |
| `src/shells.*` | Other shells and wallpaper tools: caelestia, DMS, illogical-impulse, Ambxst, HyprPanel, COSMIC, awww, hyprpaper, wpaperd, swaybg, … |
| `src/depthgen.*`, `src/depthhelper.cpp` | Depth: the model download, `undershell-depth` (which runs it) and the queue and cache of maps |
| `src/wallkind.*` | still or moving wallpaper (depth is off for videos) |
| `src/offscreen.*` | headless rendering (`--snapshot`, tests) |
| `src/perf.*` | performance profiles, the load governor, battery |
| `src/wallstore.*` | the wallpaper as a shared texture (decoded off the frame), crossfades |
| `src/wallset.*` | setting the wallpaper through each shell or tool's own command |
| `data/fonts/` | Space Grotesk, Fraunces, Inter Display, JetBrains Mono, Google Sans Flex, Bangers (OFL) |
| `integrations/noctalia/undershell/` | the Noctalia bar plugin |
| `packaging/arch/` | PKGBUILD |
| `packaging/build-release.sh`, `.github/workflows/release.yml` | the packages of every release (one container per distribution) |

## Pet artwork

`meson test -C build pets system editor docs` checks the catalogue, poses,
mirroring, opaque pixel edges, live species changes and legacy behaviour.
`build/test_pets --gallery build/pets` exports the rendered catalogue as
`bestiary.png` (Actitud), `bestiary-chibi.png` (Chibi), a transparent PNG per
companion and a `*-sheet.png` per new
species. Sheets are 256 × 768: two 128 × 128 frames in each row, ordered idle,
sit, walk, jump, dance, sleep. Walk alternates at six frames per second
(times `speed`), dance at four. Still poses repeat; blinking and facing are
runtime variants. The atlases in `art/pets/` are embedded at build time
(`tools/embed-pets.py`); how they are made from the generated pictures is in
[`art/pets/README.md`](../art/pets/README.md). No network, external files or
compositor is needed at run time.
