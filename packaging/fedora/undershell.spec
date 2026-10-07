Name:           undershell
Version:        1.1.0
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

%{?systemd_requires}

%description
Desktop widgets for Wayland compositors that support the layer-shell
protocol: audio visualizers, clocks and a now-playing card that sit above the
wallpaper and below the windows. With Noctalia they take its palette and,
through its depth plugin, pass behind the subject of the wallpaper.

%prep
%autosetup

%build
%meson -Ddev_paths=false
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
%{_datadir}/%{name}/
%{_userunitdir}/%{name}.service

%changelog
* Mon Oct 05 2026 EternalSelf-2328 <71615506+EternalSelf-2328@users.noreply.github.com> - 1.1.0-1
- First Fedora package
