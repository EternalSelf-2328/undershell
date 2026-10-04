# Credits and licences

undershell, © 2026 EternalSelf-2328, is free software under the **GNU GPL v3
or later** (see `LICENSE`).

- **Ryoku** — <https://github.com/ryoku-dev/ryoku>, GPL-3.0. The visualizer
  looks (`shaders/spectrum.frag`, ported from `ryoku/ui/shaders/spectrum.frag`),
  the spectrum motion (`Motion.qml`, `spectrum.js`), the clock faces, the
  now-playing card, the faux-kana alphabet and the album-accent formula are
  translations of Ryoku's QML/GLSL/JS.
- **Noctalia** — <https://github.com/noctalia-dev/noctalia>, MIT,
  © 2026 noctalia-dev. `src/audio.cpp` adapts the spectrum analysis of
  `src/pipewire/pipewire_spectrum.cpp`; `src/overlay.cpp` uses the wallpaper
  sampling maths of `wallpaper_sampling_glsl.h` so depth masks line up exactly.
  `protocols/wlr-layer-shell-unstable-v1.xml` is the copy shipped by Noctalia
  (wlroots protocol, MIT).
- **Fonts** (SIL Open Font License 1.1, texts in `data/fonts/`): Space
  Grotesk, Fraunces, JetBrains Mono, and Inter (the "Inter Display" files are
  static instances of Inter's variable font at opsz 32).
- Lyrics come from **LRCLIB** (<https://lrclib.net>); weather from Noctalia's
  own cache.

## Sung

`src/m3shapes.cpp` ports the Material 3 shape library from Sung
(https://github.com/yappologistic/Sung, `src/m3shape.cpp`), MIT licence,
Copyright (c) yappologistic — itself a port of androidx.graphics.shapes'
RoundedPolygon and MaterialShapes.kt (Apache-2.0, The Android Open Source
Project).

## Google Sans Flex

`data/fonts/GoogleSansFlex.ttf` is Google Sans Flex (variable: wght, wdth,
ROND, slnt, opsz, GRAD), Copyright 2015 The Google Sans Flex Authors, under
the SIL Open Font License 1.1 (`data/fonts/OFL-GoogleSansFlex.txt`). The
poster lyric line follows Sung's `qml/PosterLine.qml` (MIT, (c) yappologistic).
