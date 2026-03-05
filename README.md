# hyprexpose

[![AUR](https://img.shields.io/aur/version/hyprexpose-git)](https://aur.archlinux.org/packages/hyprexpose-git)
![vibecoded](https://img.shields.io/badge/vibecoded-ff69b4?style=flat&logo=sparkles&logoColor=white)

Lightweight workspace overview for [Hyprland](https://hyprland.org). Shows active workspaces with real window thumbnails, navigate with keyboard, press Enter to switch.

```
┌───────────────────────────────────────────┐
│              (dimmed bg)                  │
│                                           │
│  ┌──── 1 ────────┐  ┌──── 2 ────────┐   │
│  │ ┌──────┐┌───┐ │  │ ┌───────────┐ │   │
│  │ │firefo││foot│ │  │ │  spotify  │ │   │
│  │ │  x   ││    │ │  │ │  player   │ │   │
│  │ └──────┘└───┘ │  │ └───────────┘ │   │
│  │  [selected]   │  │               │   │
│  └───────────────┘  └───────────────┘   │
│                                           │
└───────────────────────────────────────────┘
```

## Features

- Real window thumbnails via hyprland-toplevel-export protocol
- Fullscreen overlay using wlr-layer-shell
- Keyboard navigation (arrow keys / hjkl)
- Runs as a daemon, toggled with SIGUSR1 (~0% CPU when hidden)
- Direct IPC via Hyprland's unix socket (no process spawning)
- No runtime dependencies beyond Hyprland itself

## Dependencies

**Build:** `wayland-scanner`

**Runtime:** `wayland-client` `cairo` `pango`

On Arch:

```
pacman -S wayland cairo pango
```

## Build

```
make
```

## Usage

Start the daemon:

```
./hyprexpose &
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
| Escape | Close overlay |

## How it works

hyprexpose runs as a background daemon that does nothing until it receives SIGUSR1. On signal, it:

1. Queries Hyprland's IPC socket for active workspaces and clients
2. Captures window thumbnails via the hyprland-toplevel-export protocol
3. Renders workspace cards with Cairo/Pango onto a wlr-layer-shell overlay
4. Waits for keyboard input, then switches workspace and hides

## License

MIT
