# Desktop, shells and power

Part of [undershell](../README.md)'s documentation.

## Screensaver

After a while without input, undershell can cover every screen with a look
of its own until you move the mouse: by default the time in pixels (the
`pixels` look with `@TIME@`) over black, or any visualizer look and a clock
over the darkened wallpaper. It is off unless you give it a time:

```toml
[screensaver]
idle = 300                # seconds without input; 0 (the default): off
background = "black"      # black | wallpaper (darkened by dim = 0.6)
while_playing = false     # with music or a video playing it waits for the pause
# output = "HDMI-A-1"     # only on that screen (default: all)

[screensaver.look]        # any visualizer keys (style = "none": no look)
style = "pixels"
pixels_text = "@TIME@"

[screensaver.clock]       # any clock keys; it shows once you name a face
face = "flip"
```

It needs a compositor with `ext-idle-notify` (tried on Umbriel; `msg status`
says so when the compositor lacks it), which also keeps it away while a
program inhibits idling (a video in full screen). It sits on the overlay
layer and takes the pointer, so the click that wakes it reaches no window;
a key also wakes it, but that key still goes to the focused window. Your
shell's lock screen goes over it. While it shows it costs what its look
costs at 30 fps (the time in pixels: about 2 % of GPU and 4 % of CPU on an
HD 520 at 1366×768); while it waits, nothing. `undershell msg screensaver`
shows it now and `msg screensaver-stop` takes it down; `msg status` says
whether it is on.

## Performance profiles

`performance` in `[general]` (also in the editor's help card, or
`undershell msg perf <level>`) sets how much undershell may spend:

| Level | Frame rate | Costly widgets drawn at | Particles | Effects |
|---|---|---|---|---|
| `eco` | 30 fps | half resolution | 40 % | lowest quality, no bloom |
| `balanced` | 60 fps | 75 % | 70 % | medium |
| `high` | as each widget asks | full | all | high |
| `ultra` | as each widget asks | full | 150 % | highest |

`auto` (the default) starts at `high` and measures what the widgets cost
every second (GPU time with `GL_EXT_disjoint_timer_query`, CPU time of the
frames): over a fifth of the time for two seconds, it steps down; when there
is room for the next level for a while, it steps back up. A widget only
counts as costly (and is drawn at the reduced resolution, scaled back up by
the compositor) when its own frames take over ~1 ms at full resolution, so
text and clocks stay sharp. On battery it goes one level lower
(`on_battery = "lower"`, or `"eco"`, or `"keep"`).

Covered widgets cost nothing where the compositor stops asking hidden
surfaces for frames (wlroots-based ones such as Sway do; tested with a
fullscreen window): their drawing stops, and while no visible widget uses
the spectrum the audio analysis pauses too.

Measured on an Intel HD 520 with the Fire look at 1400×700: about 17 % of
the GPU on `high`, 2 % on `eco`. `undershell msg perf` shows the level, the
load and what each widget costs.

## Without Noctalia (KDE Plasma and others)

undershell takes the wallpaper, its fill mode and the colours from the first
of these that is there:

1. **`[general] wallpaper`** (and `wallpaper_fill`) in `config.toml`, if set:
   over any of the others.
2. **KDE Plasma**, inside a Plasma session (or when nothing else is found):
   each screen's desktop wallpaper (`plasma-org.kde.plasma.desktop-appletsrc`,
   the current activity, Plasma's numbering of the screens from
   `kwinoutputconfig.json`; for a wallpaper package such as the default
   *Next*, the image that fits the screen and its dark variant with a dark
   colour scheme), its fill mode, and a palette from the colour scheme and
   accent colour in `kdeglobals` (`primary` is the accent). A change in
   Plasma's settings is picked up at once. A slideshow keeps its current image
   to itself, so it has no depth.
3. **GNOME**, inside a GNOME session: its wallpaper and accent colour (see
   [GNOME](#gnome)).
4. **Noctalia**, when it is running (or installed and nothing below is).
5. **Another shell or wallpaper tool that is running**, read from its own
   files (each format taken from the project's source):

   | | Wallpaper | Colours |
   |---|---|---|
   | [caelestia](https://github.com/caelestia-dots/shell) | `~/.local/state/caelestia/wallpaper/path.txt` | `scheme.json` |
   | [DankMaterialShell](https://github.com/AvengeMedia/DankMaterialShell) | `session.json` (per monitor, fill mode) | `dms-colors.json` |
   | [end-4's illogical-impulse](https://github.com/end-4/dots-hyprland) | `~/.config/illogical-impulse/config.json` | `generated/colors.json` |
   | [Ambxst](https://github.com/Axenide/Ambxst) | `~/.cache/ambxst/wallpapers.json` | `colors.json` |
   | [HyprPanel](https://github.com/Jas-SinghFSU/HyprPanel) | `config.json` (`wallpaper.image`) | — |
   | COSMIC | cosmic-bg's state, per output | — |
   | mpvpaper | its command line (a video: no depth) | — |
   | awww / swww | its cache, per output, with the resize mode | pywal |
   | hyprpaper | `hyprctl hyprpaper listactive`, `hyprpaper.conf` | pywal |
   | wpaperd | its links in `~/.local/state/wpaperd` | pywal |
   | swaybg | its command line | pywal |
   | waypaper | `config.ini` / `state.ini` | pywal |

   "pywal" means `~/.cache/wal/colors.json`, used when it was made from that
   same wallpaper. A shell or tool that starts or stops is noticed within a
   few seconds.

The music card reads the MPRIS players directly when Noctalia's aggregator is
not on the bus. `undershell msg status` shows which source is in use
(`source=plasma`, `source=noctalia`, `+manual`).

### Setting the wallpaper through any of them

`undershell msg wallpaper-set <picture> [output]` asks whichever program draws
the wallpaper to show another picture, with that program's own command (no
shell in between; each form taken from the project's source). undershell
never replaces the program you use:

| Source | Command it runs | Per output |
|---|---|---|
| Noctalia | `noctalia msg wallpaper-set [output] <path>` | yes |
| skwd-wall (when `skwd-walld` runs) | `skwd-helm apply <path> [-o output]` | yes |
| caelestia | `caelestia wallpaper --file=<path>` | one for all |
| DankMaterialShell | `dms ipc call wallpaper set <path>` / `setFor <output> <path>` | yes |
| illogical-impulse | `switchwall.sh --image <path>` | one for all |
| Ambxst | `ambxst wallpaper <path> [-monitor output]` (copies the picture into its folder) | yes |
| HyprPanel | `hyprpanel setWallpaper <path>` (paths without spaces or shell characters) | one for all |
| awww / swww | `awww img [-o output] --resize <fill> -- <path>` | yes |
| hyprpaper 0.8 | `hyprctl hyprpaper wallpaper "<output>,<path>,<fit>"` | yes |
| wpaperd | `wpaperctl set <path> [output]` | yes |
| swaybg | started again with every screen's picture; the old one ends a second later | yes |
| waypaper | `waypaper --wallpaper=<path> --fill <fill> [--monitor output]` | yes |
| KDE Plasma | `plasma-apply-wallpaperimage -f <fill> <path>` | one for all |
| GNOME | `gsettings set org.gnome.desktop.background picture-uri[-dark]` | one for all |

COSMIC and mpvpaper are not covered yet. The fill mode in use is kept. A
widget that draws the wallpaper again (the filter) crossfades to the new
picture.

### GNOME

GNOME (Mutter) does not implement layer-shell, so a client cannot put itself
on the desktop. undershell ships a small GNOME Shell extension (GNOME 45–51)
that does it the way Desktop Icons NG does: the extension starts undershell
as its own Wayland client, and each widget, a plain window there, becomes a
*desktop* window (below every window, on every workspace, out of Alt+Tab and
the overview, clicks passing through to the desktop) at the place its title
names. The editor is laid over the whole monitor, above the rest, while it is
open. undershell reads GNOME's wallpaper (`picture-uri`, the dark one under a
dark style, `picture-options`) and makes its palette from the accent colour
(GNOME 47+) and the style; a change is picked up at once (dconf's signal).

1. Install undershell (any of the ways above; the extension is installed to
   `…/share/gnome-shell/extensions/undershell@eternalself-2328.github.io`).
2. **Log out and back in** (GNOME Shell on Wayland only sees new extensions
   at login), then enable it:
   ```sh
   gnome-extensions enable undershell@eternalself-2328.github.io
   ```
   or in the Extensions app. It starts undershell; there is no need for the
   systemd service on GNOME (started there, it sees no layer-shell and exits).
3. `undershell msg edit` (e.g. on a keyboard shortcut in GNOME's settings)
   opens the editor as anywhere else.

The widgets are hidden in the overview, like other desktop windows. A
wallpaper slideshow (an `.xml`) has no single picture, so it has no depth.

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
