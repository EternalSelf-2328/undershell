project('undershell', 'cpp',
  version : '0.1.0',
  license : 'GPL-3.0-or-later',
  default_options : ['cpp_std=c++20', 'warning_level=2', 'buildtype=release', 'b_ndebug=if-release'])

wayland_client = dependency('wayland-client')
wayland_egl    = dependency('wayland-egl')
wayland_cursor = dependency('wayland-cursor')
wayland_protos = dependency('wayland-protocols')
egl            = dependency('egl')
glesv2         = dependency('glesv2')
pipewire       = dependency('libpipewire-0.3')
tomlpp         = dependency('tomlplusplus')
glib           = dependency('glib-2.0')
cairo          = dependency('cairo')
xkbcommon      = dependency('xkbcommon')

scanner = find_program('wayland-scanner')
wl_protos_dir = wayland_protos.get_variable('pkgdatadir')

protocols = [
  meson.project_source_root() / 'protocols/wlr-layer-shell-unstable-v1.xml',
  wl_protos_dir / 'stable/xdg-shell/xdg-shell.xml',
]
proto_src = []
foreach p : protocols
  name = p.split('/')[-1].replace('.xml', '')
  proto_src += custom_target(name + '-client-header',
    input : p, output : name + '-client-protocol.h',
    command : [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  proto_src += custom_target(name + '-code',
    input : p, output : name + '-protocol.c',
    command : [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach

add_languages('c', native : false)

executable('undershell',
  proto_src,
  'src/main.cpp',
  'src/app.cpp',
  'src/gl.cpp',
  'src/config.cpp',
  'src/noctalia.cpp',
  'src/depth.cpp',
  'src/audio.cpp',
  'src/visualizer.cpp',
  'src/overlay.cpp',
  'src/ipc.cpp',
  dependencies : [wayland_client, wayland_egl, wayland_cursor, egl, glesv2, pipewire, tomlpp, glib, cairo, xkbcommon],
  install : true)
