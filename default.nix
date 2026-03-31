{ lib
, rustPlatform
, fetchFromGitHub
, pkg-config
, wayland
, wayland-protocols
, cairo
, pango
, glib
, libxkbcommon
, fontconfig
, wayland-scanner
, gobject-introspection
}:

rustPlatform.buildRustPackage rec {
  pname = "hyprexpose";
  version = "ba81e47";

  src = fetchFromGitHub {
    owner = "ThiagoAVicente";
    repo = "hyprexpose";
    rev = "ba81e47f00f1da941864d51de1a3f677c98f3c96";
    hash = "sha256-ezNCERdDSBr8avQa38A4iXDG3lTkqqstUONINwu7pfg=";
  };

  cargoHash = "sha256-eymgYj6WlrFHS7TBAv3jQVb6aJGBM3lt6WlZf4Esi14=";

  nativeBuildInputs = [
    pkg-config
    wayland-scanner
    gobject-introspection
  ];

  buildInputs = [
    wayland
    wayland-protocols
    cairo
    pango
    glib
    libxkbcommon
    fontconfig
  ];

  postInstall = ''
    install -Dm644 config.example.toml $out/share/hyprexpose/config.example.toml
  '';

  meta = with lib; {
    description = "Lightweight workspace overview for Hyprland";
    homepage = "https://github.com/ThiagoAVicente/hyprexpose";
    license = licenses.mit;
    platforms = platforms.linux;
    mainProgram = "hyprexpose";
  };
}
