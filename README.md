# hermes-hyprland-plugin

A Hyprland compositor plugin that exposes window management and input control via Unix socket. Built for Hermes Agent remote desktop control.

## What it does

Exposes a Unix socket at `/tmp/hermes-hyprland.sock` that allows external tools to:
- Get all windows with exact compositor-level positions
- Get/move cursor position
- Focus windows
- Query active window

This is compositor-level access -- more accurate than hyprctl and works without subprocess overhead.


## Install Dependencies (Arch Linux)

    sudo pacman -S hyprland nlohmann-json libdrm pixman cmake

## Build

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build

## Load

    hyprctl plugin load ./build/hermes-hyprland.so

## Auto-load on startup

Add to ~/.config/hypr/hyprland.conf:

    plugin = /path/to/hermes-hyprland.so

## Socket API

Send JSON to /tmp/hermes-hyprland.sock

### Actions

| Action | Params | Returns |
|--------|--------|---------|
| ping | - | {success, message} |
| get_windows | - | {success, windows[]} |
| get_cursor | - | {success, cursor{x,y}} |
| get_active | - | {success, class, title, x, y, width, height} |
| move_cursor | x, y | {success, x, y} |
| focus_window | class or title | {success, focused} |

## Requirements

- Hyprland 0.55+
- nlohmann-json
- libdrm
- pixman

## Used by

- hermes-backup: https://github.com/xCaptaiN09/hermes-backup
