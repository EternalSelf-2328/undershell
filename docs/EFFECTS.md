# Writing your own wallpaper effects

The wallpaper widget (`type = "filter"`, "Wallpaper & effects" in the editor) can move the
wallpaper: water, wind, light rays, a 2.5D camera… Each effect is a small piece of GLSL
with a JSON header that declares its options. The editor's inspector builds the rows for
those options by itself, so an effect you write shows up in the editor like the
built-in ones.

Put your effect in `~/.config/undershell/effects/<name>.glsl`. The file name is the
effect's id: a–z, 0–9 and `_`, up to 24 characters. It is picked up as soon as you save it.
A file with the same name as a built-in effect replaces that effect.

## A complete example

```glsl
/* undershell-effect
{
  "name": {"en": "Heat haze", "es": "Calima"},
  "stage": "warp",
  "uses": ["time"],
  "params": [
    {"key": "speed", "kind": "number", "min": 0, "max": 3, "step": 0.05, "default": 1,
     "label": {"en": "Speed", "es": "Velocidad"}}
  ]
}
*/
// air shimmering over hot ground: a fine wobble that rises
vec2 warp(vec2 uv) {
    vec2 q = vec2(uv.x * u_aspect, uv.y) * 80.0;
    vec2 o = flow(q + vec2(0.0, u_time * speed * 3.0));
    return uv + pictureOffset(o * 0.0015);
}
```

Turn it on with `fx_heat = true` in the widget's block, or with the switch in the editor.
Effects also work on any other widget (a clock, a picture, text): there `uv` covers the
widget's own box, `wall()` reads the widget's own picture, and "where" is everywhere or
above / below a line (the depth and the painted masks belong to the wallpaper).

## The header

| Field | Meaning |
|---|---|
| `name` | `{"en": …, "es": …}` or one string: the switch's label |
| `stage` | `"warp"`: the effect bends **where** the picture is read, with `vec2 warp(vec2 uv)`. `"colour"` (or `"color"`): it changes **what** was read, with `vec3 color(vec3 c, vec2 uv)`. `"light"`: something soft laid **over** the picture (rays, fog, cloud shadows), with `vec4 light(vec2 uv)`: the colour to add (already multiplied by how much it covers) and how much it covers what is under it (0 adds light, 1 hides the picture). Lights are drawn on a surface a quarter to a half of the size and stretched over the sharp picture, so they can take many samples cheaply |
| `uses` | any of `"time"` (it moves by itself, so frames keep coming), `"audio"` (it follows the music), `"depth"` (it reads the wallpaper's depth) |
| `audio_when` | the key of one of its `bool` options: it listens to the music only while that option is on |
| `params` | its options, up to 16 (below) |

Each option:

| Field | Meaning |
|---|---|
| `key` | its name in your GLSL and in the config (`fx_<effect>_<key>`): a–z, 0–9 and `_`, starting with a letter, and not a GLSL word or one of the names below |
| `kind` | `number` (a `float`), `bool` (a `bool`), `choice` (an `int`: the index of the chosen option) or `color` (a `vec3`) |
| `label` | `{"en": …, "es": …}` or one string; keep it short (it is a row label) |
| `min`, `max`, `step`, `default`, `integer` | for `number` |
| `default` | for `bool` (`true`/`false`), `choice` (one of its options) and `color` (`#rrggbb` or a palette role: `primary`, `secondary`, `tertiary`, `on_surface`…) |
| `options`, `option_names` | for `choice`: the values (a–z, 0–9, `_`), and optionally `{"en": […], "es": […]}` to show |

Every effect also gets three options of its own, without declaring them: `strength` (0–2),
`mask` (where it applies: everywhere, the background, the subject, above or below a line,
or one of four masks you paint) and `split` (the line's height, 0 at the top to 1 at the
bottom). `background` and `subject` follow the wallpaper's depth (undershell's depth model);
without one, `subject` is empty.

The painted masks (`paint1` … `paint4`) are painted in the editor with the depth brush:
choose **1**–**4** under "Paints on" (or press "Paint the mask…" in the effect's rows), then
the brush, wand or lasso add to the mask or take out of it. Each wallpaper has its own four,
kept in `~/.config/undershell/effect-masks/<sha256>.usfm`; one mask can serve several
effects (the water for waves and ripples, the sky for mist).

## What your GLSL can use

GLSL ES 3.00. Write only functions and constants: no `uniform`, `in`, `out`, `precision` or
`#` lines. Your options are declared for you. Everything you declare is renamed, so two
effects never clash.

| Name | What it is |
|---|---|
| `uv` | where in the picture: 0..1 across and down |
| `u_time` | seconds since the widget started |
| `u_audio` | `vec4`: bass, mids, treble, beat (each 0..1) |
| `u_quality` | `int` 0..3: the performance level (eco … ultra); use it for the number of samples or octaves |
| `u_aspect` | the picture's width / height |
| `u_screen` | the surface's size in pixels |
| `screenPx` | this pixel's position on the surface, in pixels (in a light, on its smaller surface) |
| `wall(uv)`, `wallLod(uv, lod)` | the picture at a point (`wallLod` reads a smaller, softer copy: cheaper for many samples) |
| `depth(uv)`, `subject(uv)` | the depth there (0 far … 1 near; 0 without a depth map), and whether it is the subject (0..1) |
| `noise(p)`, `noise4(p)`, `flow(p)`, `fbm(p)` | smooth noise: 0..1, four independent channels, a random direction in −1..1, layered noise (more layers at higher quality) |
| `hash(p)` | a random 0..1 for a point |
| `luma(c)` | a colour's brightness |
| `pictureOffset(d)` | turns an offset measured in picture heights into a `uv` offset (so a circle stays round) |

## Cost

All warps and colours that are on are joined into one shader and drawn in one pass, at the
effects' frame rate (`fx_fps`, 30 by default); all lights share a second, smaller pass. A
`warp` and a simple `color` cost little; each `wall()` you read costs a texture read for
every pixel on screen, so loop over them only as much as `u_quality` allows, and put
anything soft that needs many samples in a `light` (the built-in light rays take 12 samples
on eco and 36 on ultra, on a surface 1/16 to 1/4 of the screen).

Measured on an Intel HD 520 at 1920×1080 (GPU time per frame; the picture alone is 1.5 ms):
a warp adds 0.5–1.3 ms, a colour 0.4–1.5 ms, the light rays 1.4 ms and the mist 0.8 ms
(balanced quality). At 30 frames a second, that is 5–9 % of that GPU.

## When something is wrong

A header that cannot be read, or GLSL that does not compile, marks the effect with
"(error)" in the editor and leaves it out; the other effects still draw. The reason is in
undershell's log (`journalctl --user -u undershell`).
