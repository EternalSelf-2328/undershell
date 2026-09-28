project('undershell', 'cpp', 'c',
  version : '1.0.0',
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
wayland_protos = dependency('wayland-protocols')

datadir = get_option('prefix') / get_option('datadir') / 'undershell'
add_project_arguments(
  '-DUS_INSTALL_DATA_DIR="' + datadir + '"',
  '-DUS_VERSION="' + meson.project_version() + '"',
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
  'src/canvas.cpp',
  'src/clock.cpp',
  'src/clockparts.cpp',
  'src/config.cpp',
  'src/depth.cpp',
  'src/depthfield.cpp',
  'src/depthpaint.cpp',
  'src/docgen.cpp',
  'src/editor.cpp',
  'src/gl.cpp',
  'src/inspector.cpp',
  'src/ipc.cpp',
  'src/jobs.cpp',
  'src/media.cpp',
  'src/nowplaying.cpp',
  'src/noctalia.cpp',
  'src/offscreen.cpp',
  'src/overlay.cpp',
  'src/profiles.cpp',
  'src/saves.cpp',
  'src/text.cpp',
  'src/visualizer.cpp',
  'src/wallkind.cpp',
  'src/widget.cpp',
  dependencies : deps)
core_dep = declare_dependency(link_with : core, include_directories : include_directories('src'), dependencies : deps)

executable('undershell', 'src/main.cpp', dependencies : core_dep, install : true)
install_subdir('data/fonts', install_dir : datadir)

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

foreach t : ['config', 'depth', 'docs', 'editor', 'media', 'motion', 'render']
  test(t, executable('test_' + t, 'tests/test_' + t + '.cpp', dependencies : core_dep, cpp_args : test_args),
       env : ['UNDERSHELL_DATA_DIR=' + meson.project_source_root() / 'data'])
endforeach
