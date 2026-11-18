{ pkgs ? import <nixpkgs> { }, src ? ../../. }:

let
  inherit (pkgs) lib;
  versionMatch = builtins.match ''.*version : '([0-9.]+)'.*''
    (builtins.readFile (src + "/meson.build"));
in
pkgs.stdenv.mkDerivation {
  pname = "undershell";
  version = builtins.head versionMatch;
  src = lib.fileset.toSource {
    root = src;
    fileset = lib.fileset.unions (map (file: src + file) [
      "/art"
      "/data"
      "/docs"
      "/integrations"
      "/protocols"
      "/src"
      "/tests"
      "/tools"
      "/LICENSE"
      "/NOTICE.md"
      "/README.md"
      "/meson.build"
      "/meson_options.txt"
    ]);
  };

  strictDeps = true;
  nativeBuildInputs = with pkgs; [ meson ninja pkg-config python3 wayland-scanner ];
  buildInputs = with pkgs; [
    cairo curl ffmpeg fontconfig gdk-pixbuf glib libglvnd libxkbcommon lz4
    nlohmann_json onnxruntime pango pipewire quickjs-ng sqlite systemd
    tomlplusplus wayland wayland-protocols
  ];
  mesonBuildType = "release";
  mesonFlags = [
    "-Ddev_paths=false"
    "-Dsystemd_user_unit_dir=lib/systemd/user"
    "-Dvideo=enabled"
    "-Ddepth_model=enabled"
    "-Devolution=enabled"
    "-Dscenes=enabled"
    "-Dscripts=enabled"
  ];

  doCheck = true;
  checkPhase = ''
    runHook preCheck
    TZDIR="${pkgs.tzdata}/share/zoneinfo" \
      meson test --no-rebuild --print-errorlogs config docs editor media motion wallset
    runHook postCheck
  '';

  meta = {
    description = "Wayland desktop widgets, audio visualizers and wallpaper effects";
    homepage = "https://github.com/EternalSelf-2328/undershell";
    license = lib.licenses.gpl3Plus;
    mainProgram = "undershell";
    platforms = lib.platforms.linux;
  };
}
