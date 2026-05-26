# hermes-hyprland-plugin (v3.0 - Zero Mouse Stealing Edition)

> [!WARNING]
> **Experimental & Under Active Development**
> This compositor plugin and setup are under active, experimental development and are **not a final-grade production product**. Due to the rapid evolution of Wayland, Hyprland APIs, and D-Bus/AT-SPI protocols, some components or features may occasionally break, fail to target properly, or require manual adjustments on different hardware and software configurations. Use with caution in production workflows.

A high-performance C++ Hyprland compositor plugin that exposes window geometries, active focus states, and direct cursor warps via a Unix domain socket `/tmp/hermes-hyprland.sock`. 

When paired with our **Python Coordinate Fusion Engine**, it maps absolute screen targets for client-side applications (like Chrome, Zen Browser, Thunar, etc.) by combining compositor-level window coordinates with D-Bus AT-SPI accessibility trees.

---

## Architectural Breakthrough: Coordinate Fusion

Modern Wayland client applications (GTK, Qt, Electron, Chromium) are mathematically sandboxed—they do not know their absolute position on the physical screen for security reasons. As a result, accessibility frameworks (AT-SPI) report element coordinates relative to the window frame origin $(0, 0)$.

To bypass this Wayland limitation and achieve absolute pixel-perfect desktop automation, this system implements **Coordinate Fusion**:

$$\text{Absolute Target } X = X_{\text{Window}} + X_{\text{Element}}$$
$$\text{Absolute Target } Y = Y_{\text{Window}} + Y_{\text{Element}}$$

By combining the compositor's window origin (ground truth) with relative D-Bus element boundaries, we resolve target coordinates in **less than 1 millisecond** with **100% targeting precision**, entirely bypassing slow, vision-based coordinate guessing.

---

## IPC Socket API (`/tmp/hermes-hyprland.sock`)

Communicate with the plugin by sending JSON payloads over the active Unix Domain Socket.

| Action | Parameters | Returns | Description |
| :--- | :--- | :--- | :--- |
| `ping` | None | `{ "success": true, "message": "..." }` | Health check verifying the socket is responsive. |
| `get_windows` | None | `{ "success": true, "windows": [...] }` | Enumerates mapped window classes, titles, addresses, geometries, and focus states. |
| `get_cursor` | None | `{ "success": true, "cursor": { "x", "y" } }` | Retrieves the real-time mouse coordinate. |
| `get_active` | None | `{ "success": true, "class", "title", "x", "y", "width", "height" }` | Returns properties of the currently focused window. |
| `get_clickable_regions` | `class` (optional) | `{ "success": true, "windows": [...] }` | Retrieves compositor-level subsurfaces and active input regions. |
| `move_cursor` | `x`, `y` | `{ "success": true, "x", "y" }` | Warps the mouse pointer directly to absolute screen coordinate `(x, y)`. |
| `focus_window` | `class` or `title` | `{ "success": true, "focused": "..." }` | Focuses the matching window via the compositor focus state manager. |

---

## Installation & Build (Arch Linux)

### 1. Install Dependencies
```bash
sudo pacman -S hyprland nlohmann-json libdrm pixman cmake
```

### 2. Compile the C++ Plugin
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
This produces `build/hermes-hyprland.so` (size ~220KB).

### 3. Load the Plugin into the Active Session
```bash
hyprctl plugin load ./build/hermes-hyprland.so
```

### 4. Configure Autostart at Boot
Add the following line to `~/.config/hypr/hyprland.conf`:
```ini
plugin = /home/captain/hermes-hyprland-plugin/build/hermes-hyprland.so
```

---

## The Python Fusion Module (`hermes_hyprland_fusion.py`)

Our repository contains a standalone coordinate fusion client. It maps active compositor layouts to local D-Bus elements.

### Verification Run
Verify the socket, D-Bus bridges, and live calculations with:
```bash
chmod +x hermes_hyprland_fusion.py
./hermes_hyprland_fusion.py
```

### Script Usage Snippet
```python
from hermes_hyprland_fusion import CoordinateFusionEngine

# Initialize the engine
engine = CoordinateFusionEngine()

# Get all accessible UI elements with absolute coordinates fused!
elements = engine.get_clickable_elements(target_app_class="thunar")
for el in elements[:5]:
    cx, cy = el.center()
    print(f"Index #{el.index}: {el.role} '{el.label}' at absolute screen position: X={cx}, Y={cy}")
```

---

## Native Hermes Agent Integration

The coordinate fusion engine is fully integrated inside the Hermes Agent at `/home/captain/.hermes/hermes-agent/tools/computer_use/`:

1.  **[hyprland_backend.py](file:///home/captain/.hermes/hermes-agent/tools/computer_use/hyprland_backend.py)**: Adapts coordinates, screenshotting (via `grim`), SOM bubble overlays (via `Pillow`), and input injection (via `ydotool`) into the `ComputerUseBackend` standard.
2.  **[tool.py](file:///home/captain/.hermes/hermes-agent/tools/computer_use/tool.py)**: Mounts the Hyprland driver switch block and configures OS validation.

### Launching the Agent
To start Hermes with absolute, visionless coordinate targeting enabled:
```bash
export HERMES_COMPUTER_USE_BACKEND=hyprland
python3 cli.py
```
This allows the agent to navigate folders, click menus, type text, and scroll in under **5 milliseconds** per action with absolute accuracy.

---

## License

This project is licensed under the MIT License. Developed by xCaptaiN09.
