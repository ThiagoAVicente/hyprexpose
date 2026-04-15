{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    pkg-config
    cargo
    rustc
    rust-analyzer
    wayland-scanner
    gobject-introspection
  ];

  buildInputs = with pkgs; [
    wayland
    wayland-protocols
    cairo
    pango
    glib
    libxkbcommon
    fontconfig
  ];

  shellHook = ''
    export LD_LIBRARY_PATH=${pkgs.lib.makeLibraryPath (with pkgs; [
      wayland
      cairo
      pango
      glib
      libxkbcommon
      fontconfig
    ])}:$LD_LIBRARY_PATH
  '';
}
