# hyprexpose

[![AUR](https://img.shields.io/aur/version/hyprexpose-git)](https://aur.archlinux.org/packages/hyprexpose-git)
![vibecoded](https://img.shields.io/badge/vibecoded-ff69b4?style=flat&logo=sparkles&logoColor=white)

Lightweight workspace overview for [Hyprland](https://hyprland.org). Shows active workspaces with real window thumbnails, navigate with keyboard, press Enter to switch.

![Demo](demo.gif)

## Features

- Real window thumbnails via hyprland-toplevel-export protocol
- Fullscreen overlay using wlr-layer-shell
- Keyboard navigation (arrow keys / hjkl)
- Move the active window to another workspace with `m`
- Runs as a daemon, toggled with SIGUSR1 (~0% CPU when hidden)
- Direct IPC via Hyprland's unix socket (no process spawning)
- TOML config file for colors, fonts, and behavior

## Dependencies

**Build:** Rust toolchain (`cargo`), `libwayland-dev`, `libpango1.0-dev`

On Arch:

```
pacman -S rust wayland pango
```

On Ubuntu/Debian:

```
apt install cargo libwayland-dev libpango1.0-dev
```

## Build

```
cargo build --release
```

The binary is at `target/release/hyprexpose`.

## Usage

Start the daemon:

```
hyprexpose &
```

Disable window previews (uses colored rectangles instead):

```
hyprexpose --no-preview &
```

Toggle the overlay:

```
pkill -SIGUSR1 hyprexpose
```

Add to your Hyprland config:

```ini
exec-once = hyprexpose
bind = $mainMod, Tab, exec, pkill -SIGUSR1 hyprexpose
```

## Install (AUR)

```
yay -S hyprexpose-git
```

## Controls

| Key | Action |
|---|---|
| Arrow keys / hjkl | Navigate workspaces |
| Enter | Switch to selected workspace |
| `m` | Move active window to selected workspace |
| Escape | Close overlay |

## Configuration

Copy the example config and edit to taste:

```
mkdir -p ~/.config/hyprexpose
cp config.example.toml ~/.config/hyprexpose/config.toml
```

Config is loaded from `$XDG_CONFIG_HOME/hyprexpose/config.toml` (falls back to `~/.config/hyprexpose/config.toml`). All fields are optional — defaults are used for anything not specified.

See [`config.example.toml`](config.example.toml) for all available options with inline documentation.

## How it works

hyprexpose runs as a background daemon that does nothing until it receives SIGUSR1. On signal, it:

1. Queries Hyprland's IPC socket for active workspaces and clients
2. Captures window thumbnails via the hyprland-toplevel-export protocol
3. Renders workspace cards with Cairo/Pango onto a wlr-layer-shell overlay
4. Waits for keyboard input, then switches workspace (or moves a window) and hides

## License

MIT
