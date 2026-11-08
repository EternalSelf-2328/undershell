Name:           undershell
Version:        1.6.1
# built from a checkout (build-rpm.sh), the commit count goes in the release
# so every update is newer to dnf; a release tarball builds plain -1
Release:        1%{?_gitcount:.git%{_gitcount}}%{?dist}
Summary:        Desktop widgets that live under your Wayland shell

# GPL-3.0-or-later: undershell (and the Ryoku code it ports)
# MIT: code adapted from Noctalia and Sung, the layer-shell protocol
# OFL-1.1: the bundled fonts
License:        GPL-3.0-or-later AND MIT AND OFL-1.1
URL:            https://github.com/EternalSelf-2328/undershell
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  meson
BuildRequires:  systemd-rpm-macros
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-egl)
BuildRequires:  pkgconfig(wayland-cursor)
BuildRequires:  pkgconfig(wayland-protocols)
BuildRequires:  pkgconfig(wayland-scanner)
BuildRequires:  pkgconfig(egl)
BuildRequires:  pkgconfig(glesv2)
BuildRequires:  pkgconfig(libpipewire-0.3)
BuildRequires:  pkgconfig(tomlplusplus)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(cairo)
BuildRequires:  pkgconfig(pangocairo)
BuildRequires:  pkgconfig(pangofc)
BuildRequires:  pkgconfig(fontconfig)
BuildRequires:  pkgconfig(xkbcommon)
BuildRequires:  pkgconfig(libsystemd)
BuildRequires:  pkgconfig(gdk-pixbuf-2.0)
BuildRequires:  pkgconfig(libcurl)
BuildRequires:  pkgconfig(nlohmann_json)
BuildRequires:  pkgconfig(sqlite3)
# the TV look's videos (Fedora's ffmpeg-free: GIF, VP9, AV1; H.264 needs RPM Fusion's)
BuildRequires:  pkgconfig(libavformat)
BuildRequires:  pkgconfig(libavcodec)
BuildRequires:  pkgconfig(libswscale)
BuildRequires:  pkgconfig(libavutil)
# depth without Noctalia: undershell-depth runs the depth model with ONNX Runtime
BuildRequires:  onnxruntime-devel
# Wallpaper Engine scenes (their LZ4-packed textures); their scripts need
# quickjs-ng, which Fedora does not package
BuildRequires:  pkgconfig(liblz4)
Recommends:     pipewire
Recommends:     (xdg-desktop-portal or zenity)

%{?systemd_requires}

%description
Desktop widgets for Wayland compositors that support the layer-shell
protocol: audio visualizers, clocks and a now-playing card that sit above the
wallpaper and below the windows. With Noctalia they take its palette and,
through its depth plugin, pass behind the subject of the wallpaper.

%prep
%autosetup

%build
%meson -Ddev_paths=false -Dvideo=enabled -Ddepth_model=enabled -Dscenes=enabled -Dscripts=disabled
%meson_build

%install
%meson_install
# the license and docs are listed below from where meson put them

%check
%meson_test

%post
%systemd_user_post %{name}.service

%preun
%systemd_user_preun %{name}.service

%files
%license %{_datadir}/licenses/%{name}/LICENSE
%{_docdir}/%{name}/
%{_bindir}/%{name}
%{_libexecdir}/%{name}-depth
%{_libexecdir}/%{name}-scene
%{_datadir}/gnome-shell/extensions/%{name}@eternalself-2328.github.io/
%{_datadir}/%{name}/
%{_userunitdir}/%{name}.service

%changelog
* Sat Nov 07 2026 EternalSelf-2328 <71615506+EternalSelf-2328@users.noreply.github.com> - 1.6.1-1
- Globe look: east on the right; the continents were mirrored east to west
- Inspector: the last row is clickable again ("+ Add effect" on the pet and others)
- Editor: the pet's label says "Pet", not "Bars"

* Fri Nov 06 2026 EternalSelf-2328 <71615506+EternalSelf-2328@users.noreply.github.com> - 1.6.0-1
- Wallpaper: with skwd v2 (or undershell-wall) running, each screen's wallpaper comes from it, not from the shell's possibly old path; changes followed on its socket (#1)
- New widgets: system monitor (CPU, memory, GPU, temperature, network, disk, battery), activity (your commits by day from local git), and a pet that walks, climbs windows, dances and sleeps
- New looks: Game of Life, lava lamp and a globe of dots lit by the real sun (32 looks)
- New effects: rain on the glass, snow on things (from the depth map), aurora; real weather on the wallpaper from Open-Meteo
- Screensaver after a while without input (ext-idle-notify), off by default
- Colours from the album cover on eight widget types
- Lyrics: karaoke word by word with enhanced LRC, and a .lrc beside a local track
- Depth: particles and effects cut at the widget's own plane; an unpainted mask means everywhere
- Music card: covers under 16 KB decode again

* Mon Oct 26 2026 EternalSelf-2328 <71615506+EternalSelf-2328@users.noreply.github.com> - 1.5.1-1
- Clock: automatic Matugen M3 accent from the wallpaper (`accent = "auto"`)
- Clock: fix m_wall initialization order so the wallpaper accent loads immediately
- Noctalia: populate pal["auto"] with the wallpaper accent for all widgets
- Fedora: package undershell-scene in /usr/libexec
- Tests: font-independent scene e2e test for minimal packaging environments

* Sun Oct 25 2026 EternalSelf-2328 <71615506+EternalSelf-2328@users.noreply.github.com> - 1.5.0-1
- Performance profiles: eco, balanced, high, ultra and auto (follows load and battery)
- Wallpaper: one shared picture with a crossfade on change; `msg wallpaper-set` sets it through whichever program draws it
- Animated wallpaper effects (water, wind, light rays, a 2.5D camera…), your own effects folder, painted masks, and effects on every widget
- Particles over the wallpaper: snow, rain, petals, fireflies, embers and more
- Wallpaper Engine scenes and videos, and a wallpaper gallery in the editor; with undershell-wall running, scenes are handed to it and their properties are set from the inspector
- Editor: the gallery grouped by kind; effects and particles added from a picker
- Music card: a GIF or a video of your own as the cover
- A wallpaper filter that changes nothing no longer draws over the wallpaper

* Sun Oct 18 2026 EternalSelf-2328 <71615506+EternalSelf-2328@users.noreply.github.com> - 1.4.0-1
- Water-wave, aura and stars visualizers (28 looks)
- Music card: a cyberpunk "cyber" layout (CRT plate, neon corners, data-stream spectrum)
- Editor: the toolbar and panels stay where they were left
- Music card: the idle note as one union shape (no seams)

* Wed Oct 14 2026 EternalSelf-2328 <71615506+EternalSelf-2328@users.noreply.github.com> - 1.3.1-1
- Editor toolbar: steps aside from widgets, can be dragged or stood up at a side, remembers where it was left

* Tue Oct 13 2026 EternalSelf-2328 <71615506+EternalSelf-2328@users.noreply.github.com> - 1.3.0-1
- Weather (Open-Meteo), calendar events (.ics, khal, Evolution), wallpaper filter; security fixes

* Tue Oct 13 2026 EternalSelf-2328 <71615506+EternalSelf-2328@users.noreply.github.com> - 1.2.0-1
- Layers panel and groups; shape, image, text, lyrics, calendar and notes widgets
- Own depth maps without Noctalia's plugin; colour picker; scroll bars
- Packages built for every release on GitHub

* Mon Oct 05 2026 EternalSelf-2328 <71615506+EternalSelf-2328@users.noreply.github.com> - 1.1.0-1
- First Fedora package
