project('undershell', 'cpp', 'c',
  version : '1.4.0',
  license : 'GPL-3.0-or-later',
  default_options : ['cpp_std=c++20', 'warning_level=2', 'buildtype=release', 'b_ndebug=if-release'])

deps = [
  dependency('wayland-client'),
  dependency('wayland-egl'),
  dependency('wayland-cursor'),
  dependency('egl'),
  dependency('glesv2'),
  dependency('libpipewire-0.3'),
  dependency('tomlplusplus'),
  dependency('glib-2.0'),
  dependency('cairo'),
  dependency('pangocairo'),
  dependency('pangofc'),
  dependency('fontconfig'),
  dependency('xkbcommon'),
  dependency('libsystemd'),
  dependency('gdk-pixbuf-2.0'),
  dependency('libcurl'),
  dependency('nlohmann_json'),
  dependency('threads'),
]
# the TV look's videos (GIFs play without it, through gdk-pixbuf)
video_deps = [
  dependency('libavformat', required : get_option('video')),
  dependency('libavcodec', required : get_option('video')),
  dependency('libswscale', required : get_option('video')),
  dependency('libavutil', required : get_option('video')),
]
have_video = true
foreach d : video_deps
  have_video = have_video and d.found()
endforeach
# Evolution's (and GNOME Calendar's) online calendars, read from its SQLite cache
sqlite_dep = dependency('sqlite3', required : get_option('evolution'))
if sqlite_dep.found()
  deps += sqlite_dep
  add_project_arguments('-DUS_HAVE_SQLITE=1', language : 'cpp')
endif
summary({'Evolution calendars (SQLite)' : sqlite_dep.found()}, section : 'Features')

if have_video
  deps += video_deps
  add_project_arguments('-DUS_HAVE_FFMPEG=1', language : 'cpp')
endif
summary({'videos (FFmpeg)' : have_video}, section : 'Features')
# depth without Noctalia's plugin: a helper that runs the depth model (only
# the helper links ONNX Runtime; the service starts it when there is work)
onnx_dep = dependency('libonnxruntime', required : get_option('depth_model'))
summary({'depth model (ONNX Runtime)' : onnx_dep.found()}, section : 'Features')
libexecdir = get_option('prefix') / get_option('libexecdir')
wayland_protos = dependency('wayland-protocols')

datadir = get_option('prefix') / get_option('datadir') / 'undershell'
add_project_arguments(
  '-DUS_INSTALL_DATA_DIR="' + datadir + '"',
  '-DUS_VERSION="' + meson.project_version() + '"',
  '-DUS_LIBEXEC_DIR="' + libexecdir + '"',
  language : 'cpp')
# running straight from the build tree finds the bundled fonts in the source
# tree; packages turn this off so no build path ends up in the binary
if get_option('dev_paths')
  add_project_arguments('-DUS_SOURCE_DATA_DIR="' + meson.project_source_root() / 'data' + '"', language : 'cpp')
endif
test_args = ['-DUS_SOURCE_DIR="' + meson.project_source_root() + '"']

scanner = find_program('wayland-scanner')
wl_protos_dir = wayland_protos.get_variable('pkgdatadir')
protocols = [
  meson.project_source_root() / 'protocols/wlr-layer-shell-unstable-v1.xml',
  wl_protos_dir / 'stable/xdg-shell/xdg-shell.xml',
  wl_protos_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml',
  wl_protos_dir / 'staging/fractional-scale/fractional-scale-v1.xml',
  wl_protos_dir / 'stable/viewporter/viewporter.xml',
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

core = static_library('undershell-core',
  proto_src,
  'src/app.cpp',
  'src/audio.cpp',
  'src/aura.cpp',
  'src/calendar.cpp',
  'src/calsources.cpp',
  'src/canvas.cpp',
  'src/clock.cpp',
  'src/clockparts.cpp',
  'src/config.cpp',
  'src/depth.cpp',
  'src/depthfield.cpp',
  'src/depthpaint.cpp',
  'src/docgen.cpp',
  'src/editor.cpp',
  'src/electric.cpp',
  'src/filepicker.cpp',
  'src/filter.cpp',
  'src/fireworks.cpp',
  'src/gl.cpp',
  'src/i18n.cpp',
  'src/ics.cpp',
  'src/image.cpp',
  'src/inspector.cpp',
  'src/layers.cpp',
  'src/ipc.cpp',
  'src/jobs.cpp',
  'src/looks.cpp',
  'src/m3shapes.cpp',
  'src/neon.cpp',
  'src/media.cpp',
  'src/nowplaying.cpp',
  'src/net.cpp',
  'src/noctalia.cpp',
  'src/notes.cpp',
  'src/plasma.cpp',
  'src/shells.cpp',
  'src/gnome.cpp',
  'src/depthgen.cpp',
  'src/colorpick.cpp',
  'src/offscreen.cpp',
  'src/overlay.cpp',
  'src/perf.cpp',
  'src/profiles.cpp',
  'src/rain.cpp',
  'src/pond.cpp',
  'src/ripples.cpp',
  'src/saves.cpp',
  'src/sfx.cpp',
  'src/shape.cpp',
  'src/speedlines.cpp',
  'src/stars.cpp',
  'src/strokes.cpp',
  'src/terminal.cpp',
  'src/textfx.cpp',
  'src/textwidget.cpp',
  'src/lyricswidget.cpp',
  'src/termscreen.cpp',
  'src/tv.cpp',
  'src/text.cpp',
  'src/video.cpp',
  'src/visualizer.cpp',
  'src/weather.cpp',
  'src/weatherfeed.cpp',
  'src/wallkind.cpp',
  'src/wallstore.cpp',
  'src/widget.cpp',
  dependencies : deps)
core_dep = declare_dependency(link_with : core, include_directories : include_directories('src'), dependencies : deps)

executable('undershell', 'src/main.cpp', dependencies : core_dep, install : true)
if onnx_dep.found()
  executable('undershell-depth', 'src/depthhelper.cpp', dependencies : [onnx_dep, dependency('gdk-pixbuf-2.0')],
             install : true, install_dir : libexecdir)
endif
install_subdir('data/fonts', install_dir : datadir)
# GNOME has no layer-shell: its extension starts undershell and places the widgets
if get_option('gnome_extension')
  install_subdir('integrations/gnome/undershell@eternalself-2328.github.io',
                 install_dir : get_option('prefix') / get_option('datadir') / 'gnome-shell/extensions')
endif

# systemd user unit: /usr/lib/systemd/user for packages, ~/.local/share/systemd/user
# (a directory systemd also searches) for user installs
prefix = get_option('prefix')
unitdir = get_option('systemd_user_unit_dir')
if unitdir == ''
  unitdir = prefix == '/usr' ? '/usr/lib/systemd/user' : prefix / get_option('datadir') / 'systemd/user'
endif
docdir = prefix / get_option('datadir') / 'doc/undershell'
unit_conf = configuration_data()
unit_conf.set('BINDIR', prefix / get_option('bindir'))
unit_conf.set('DOCDIR', docdir)
configure_file(input : 'data/undershell.service.in', output : 'undershell.service',
  configuration : unit_conf, install : true, install_dir : unitdir)
install_data('README.md', 'NOTICE.md', 'docs/OPTIONS.md', install_dir : docdir)
install_data('LICENSE', install_dir : prefix / get_option('datadir') / 'licenses/undershell')

foreach t : ['config', 'depth', 'docs', 'editor', 'media', 'motion', 'perf', 'render']
  test(t, executable('test_' + t, 'tests/test_' + t + '.cpp', dependencies : core_dep, cpp_args : test_args),
       env : ['UNDERSHELL_DATA_DIR=' + meson.project_source_root() / 'data'])
endforeach
