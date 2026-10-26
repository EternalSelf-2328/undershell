# Writing your own particle systems

The wallpaper widget (`type = "filter"`, "Wallpaper & effects" in the editor) can lay
particles over the wallpaper: snow, rain, petals, fireflies, floating dust, embers,
bubbles and sparks to the beat come with undershell. Each system is a JSON file;
yours go in `~/.config/undershell/particles/<name>.json` and show up in the editor's
"Particles" section as soon as they are saved. The file name is the system's id
(a–z, 0–9 and `_`, up to 24 characters); one with a built-in's name replaces it.

## A complete example

```json
{
  "name": {"en": "Falling leaves", "es": "Hojas que caen"},
  "max": 120, "rate": 8, "prewarm": 12, "sprite": "leaf",
  "emitter": {"shape": "box", "origin": [0.5, -0.05], "size": [1.3, 0.02]},
  "init": {"lifetime": [10, 16], "size": [10, 18], "speed": [40, 80], "direction": [70, 110],
           "color": ["#d9822b", "#8c3b12"], "alpha": [0.85, 1], "rotation": [0, 360],
           "spin": [-120, 120], "depth": [0.1, 0.95]},
  "operators": [
    {"type": "oscillate_position", "freq": [0.2, 0.4], "amount": 50},
    {"type": "fade", "in": 0.03, "out": 0.1}
  ]
}
```

Turn it on with `pt_leaves = true` in the widget's block (if the file is `leaves.json`),
or with its switch in the editor.

## The fields

Distances and sizes are in pixels of a 1080-pixel-high screen (they scale with yours);
speeds in those pixels per second; angles in degrees, 0 pointing right and 90 down.

| Field | Meaning |
|---|---|
| `name` | `{"en": …, "es": …}` or one string |
| `category` | Optional. The group it is listed under in the editor's **+ Add particles**: `"weather"`, `"nature"` or `"fire"`. Without it it goes under "Your own" |
| `max` | how many at most (at the "high" performance level and amount 1) |
| `rate` | how many are born per second |
| `prewarm` | seconds simulated before the first frame, so snow is already falling |
| `sprite` | `dot`, `glow`, `flake`, `streak`, `petal`, `bubble`, `spark` or `leaf` (drawn in the shader, no pictures) |
| `blend` | `"alpha"` (default) or `"add"` (light that adds up: fireflies, sparks) |
| `streak` | stretch each particle along its travel by this many seconds (rain, sparks) |
| `react` | how much it follows the music by default (0–1), for the `audio` operator |
| `emitter` | where they are born: `{"shape": "box", "origin": [x, y], "size": [w, h]}` or `{"shape": "sphere", "origin": [x, y], "radius": r}`, as fractions of the screen (the radius of its height) |
| `init` | what each starts with, a number or a `[low, high]` range: `lifetime` (s), `size`, `speed`, `direction`, `alpha`, `rotation`, `spin` (°/s), `depth` (0 far … 1 near: nearer ones are bigger and faster, and with "Behind the subject" the wallpaper hides those beyond it), and `color` (one or two colours to pick between) |
| `operators` | what changes as they live (below), applied in order |

| Operator | Fields |
|---|---|
| `gravity` | `value`: `[x, y]` acceleration |
| `drag` | `value`: how fast they slow down |
| `turbulence` | `scale`, `speed`, `strength`: a smooth wandering push |
| `vortex` | `center`: `[x, y]` (fractions), `strength`: around it |
| `fade` | `in`, `out`: fractions of the life to fade in and out |
| `oscillate_alpha` | `freq`: `[low, high]` per second, `amount` 0–1: twinkling |
| `oscillate_position` | `freq`, `amount` (pixels): swaying side to side |
| `size_change` | `end`: the size at the end of life, as a multiple |
| `color_change` | `end`: the colour at the end of life |
| `audio` | `band`: `bass`, `mid`, `treble` or `beat`; `rate`, `size`, `alpha`: how much each grows with it |

Every system also gets, in the editor and as `pt_<name>_<option>`: `amount`, `speed`,
`size`, `wind`, `tint`, `react` (with the music), `behind` (behind the wallpaper's
subject) and `mask` (where: everywhere, above or below a line, or one of the four masks
painted with the brush).

## Cost

The simulation runs on the CPU at a fixed step (30 or 60 times a second, by the
performance level) into arrays reserved once, and each system is one instanced draw.
Measured on an Intel HD 520: 700 snowflakes take 0.02 ms of CPU per frame, and drawing
them did not show above the noise of the measurement. The counts follow the performance
level (40 % on eco, 150 % on ultra).

A file that cannot be read is marked "(error)" in the editor; the reason is in
undershell's log.
