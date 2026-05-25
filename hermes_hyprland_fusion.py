#!/usr/bin/env python3
"""
Hermes Hyprland Coordinate Fusion Engine (HCFE)
Bridges Hyprland compositor window geometry with AT-SPI D-Bus accessibility trees.
Calculates absolute element screen coordinates for pixel-perfect Wayland automation.

Designed to be imported directly into the Hermes Agent's screen control backend.
"""

import os
import sys
import json
import socket
import subprocess
import logging
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple

# Set up logging
logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s")
logger = logging.getLogger("HermesHyprlandFusion")

# Try importing Atspi
try:
    import gi
    gi.require_version('Atspi', '2.0')
    from gi.repository import Atspi
    ATSPI_AVAILABLE = True
except Exception as e:
    logger.warning("AT-SPI GObject Introspection not available: %s", e)
    ATSPI_AVAILABLE = False


@dataclass
class UIElement:
    """One interactable element on the screen with fused absolute coordinates."""
    index: int                                         # 1-based index for agent target selection
    role: str                                          # AX role (e.g. menu, push button, entry, etc.)
    label: str = ""                                    # Element name/description
    bounds: Tuple[int, int, int, int] = (0, 0, 0, 0)   # Absolute screen bounds: (x, y, w, h)
    relative_bounds: Tuple[int, int, int, int] = (0, 0, 0, 0)  # Window-relative bounds: (x, y, w, h)
    app: str = ""                                      # Parent application name
    window_id: str = ""                                # Parent window address/id

    def center(self) -> Tuple[int, int]:
        """Calculates the absolute center point of the element for mouse clicks."""
        x, y, w, h = self.bounds
        return x + w // 2, y + h // 2


class HyprlandSocketClient:
    """IPC client communicating with the hermes-hyprland C++ plugin socket."""
    
    def __init__(self, socket_path: str = "/tmp/hermes-hyprland.sock"):
        self.socket_path = socket_path

    def send_command(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        """Sends a JSON command to the compositor plugin and returns the response."""
        if not os.path.exists(self.socket_path):
            return {"success": False, "error": f"Socket {self.socket_path} not found. Is the plugin loaded?"}
        
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
                s.connect(self.socket_path)
                s.sendall(json.dumps(payload).encode('utf-8'))
                
                # Receive response (read in blocks)
                response_data = b""
                while True:
                    chunk = s.recv(65536)
                    if not chunk:
                        break
                    response_data += chunk
                
                return json.loads(response_data.decode('utf-8'))
        except Exception as e:
            return {"success": False, "error": f"Socket IPC failure: {str(e)}"}


class CoordinateFusionEngine:
    """Fuses compositor frames and AT-SPI accessibility trees into absolute targets."""

    def __init__(self, socket_path: str = "/tmp/hermes-hyprland.sock"):
        self.client = HyprlandSocketClient(socket_path)

    def get_windows(self) -> List[Dict[str, Any]]:
        """Queries compositor for all mapped windows on the desktop."""
        res = self.client.send_command({"action": "get_windows"})
        if res.get("success"):
            return res.get("windows", [])
        logger.error("Failed to fetch windows: %s", res.get("error"))
        return []

    def get_active_window(self) -> Optional[Dict[str, Any]]:
        """Queries compositor for the currently active/focused window."""
        res = self.client.send_command({"action": "get_active"})
        if res.get("success"):
            return res
        logger.warning("Failed to fetch active window or no window in focus.")
        return None

    def warp_cursor(self, x: int, y: int) -> bool:
        """Warps the desktop cursor to the absolute screen coordinates (x, y)."""
        res = self.client.send_command({"action": "move_cursor", "x": x, "y": y})
        return bool(res.get("success"))

    def click_xy(self, x: int, y: int, button: str = "left", count: int = 1) -> bool:
        """Moves cursor to (x, y) and simulates a physical click using ydotool."""
        if not self.warp_cursor(x, y):
            logger.warning("Warp cursor failed before clicking.")
        
        # Simulate click via ydotool (0xC0 is left click, 0xC1 is right click, 0xC2 is middle click)
        btn_code = "0xC0"
        if button == "right":
            btn_code = "0xC1"
        elif button == "middle":
            btn_code = "0xC2"
        
        try:
            for _ in range(count):
                subprocess.run(["ydotool", "click", btn_code], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return True
        except Exception as e:
            logger.error("ydotool click simulation failed: %s", e)
            return False

    def list_apps_from_atspi(self) -> List[str]:
        """Queries the AT-SPI desktop bridge for names of active accessible applications."""
        if not ATSPI_AVAILABLE:
            return []
        
        try:
            desktop = Atspi.get_desktop(0)
            apps = []
            for i in range(desktop.get_child_count()):
                child = desktop.get_child_at_index(i)
                if child:
                    apps.append(child.get_name())
            return apps
        except Exception as e:
            logger.error("AT-SPI application enumeration failed: %s", e)
            return []

    def _traverse_atspi_tree(self, element, index_ref: List[int], app_name: str, window_id: str,
                             win_x: int, win_y: int, max_elements: int = 500) -> List[UIElement]:
        """Recursively parses AT-SPI sub-elements, calculating absolute coordinates."""
        elements = []
        if len(index_ref) >= max_elements:
            return elements

        try:
            name = element.get_name() or ""
            role = element.get_role_name() or ""
            
            # Retrieve bounds relative to the window frame origin (CoordType.WINDOW = 1)
            rect = element.get_extents(1)
            rx, ry, rw, rh = rect.x, rect.y, rect.width, rect.height

            # Filter out non-functional or invisible elements (out of bounds or tiny)
            if rw > 0 and rh > 0 and rx >= -10000 and ry >= -10000:
                # Mathematically fuse compositor offset with relative child bounds
                absolute_x = win_x + rx
                absolute_y = win_y + ry
                
                index_ref[0] += 1
                ui_el = UIElement(
                    index=index_ref[0],
                    role=role,
                    label=name.strip(),
                    bounds=(absolute_x, absolute_y, rw, rh),
                    relative_bounds=(rx, ry, rw, rh),
                    app=app_name,
                    window_id=window_id
                )
                elements.append(ui_el)
        except Exception:
            # Not all elements support get_extents; ignore and parse children
            pass

        # Parse children recursively
        try:
            child_count = element.get_child_count()
            for i in range(child_count):
                child = element.get_child_at_index(i)
                if child:
                    elements.extend(
                        self._traverse_atspi_tree(child, index_ref, app_name, window_id, win_x, win_y, max_elements)
                    )
        except Exception:
            pass

        return elements

    def get_clickable_elements(self, target_app_class: Optional[str] = None, max_elements: int = 300) -> List[UIElement]:
        """
        Calculates absolute positions of all interactable UI elements on the screen.
        If target_app_class is provided, filters for that specific application.
        Otherwise, targets the currently active/focused window.
        """
        if not ATSPI_AVAILABLE:
            logger.warning("AT-SPI is not loaded. Cannot perform coordinate fusion.")
            return []

        # Step 1: Query Compositor for Window Geometry
        windows = self.get_windows()
        if not windows:
            return []

        # Step 2: Determine target window and its coordinates
        target_win = None
        if target_app_class:
            app_lower = target_app_class.lower()
            for w in windows:
                if app_lower in w.get("class", "").lower() or app_lower in w.get("title", "").lower():
                    target_win = w
                    break
        else:
            # Fall back to currently focused window
            for w in windows:
                if w.get("focused"):
                    target_win = w
                    break
            # If no window reports focused, pick the top window
            if not target_win and windows:
                target_win = windows[0]

        if not target_win:
            logger.warning("No matching window found for element coordinate fusion.")
            return []

        win_x = target_win["x"]
        win_y = target_win["y"]
        win_class = target_win["class"]
        win_addr = target_win["address"]

        # Step 3: Find the matching AT-SPI application node
        desktop = Atspi.get_desktop(0)
        atspi_app = None
        for i in range(desktop.get_child_count()):
            child = desktop.get_child_at_index(i)
            if child:
                # Match class names (case-insensitive substring match)
                if win_class.lower() in child.get_name().lower() or child.get_name().lower() in win_class.lower():
                    atspi_app = child
                    break

        if not atspi_app:
            logger.warning("Could not find matching AT-SPI node for application: %s", win_class)
            return []

        # Step 4: Perform Coordinate Fusion Traversal
        index_tracker = [0]  # Reference wrapper for continuous index numbering
        fused_elements = self._traverse_atspi_tree(
            atspi_app, index_tracker, win_class, win_addr, win_x, win_y, max_elements
        )

        return fused_elements


if __name__ == "__main__":
    # Self-test execution showing the fusion math live
    print("=====================================================================")
    print("   Hermes Hyprland Coordinate Fusion Engine - Verification Run")
    print("=====================================================================")
    
    if not ATSPI_AVAILABLE:
        print("ERROR: gi.repository.Atspi library is not available. Verification aborted.")
        sys.exit(1)

    engine = CoordinateFusionEngine()
    
    # 1. Enumerate mapped windows
    print("\n1. Compositor Windows:")
    windows = engine.get_windows()
    for w in windows:
        focus_indicator = " [*ACTIVE*]" if w.get("focused") else ""
        print(f"  - App Class: {w['class']:{15}} Title: \"{w['title']}\"{focus_indicator}")
        print(f"    Position:  X={w['x']}, Y={w['y']} (W={w['width']}, H={w['height']})")

    # 2. Check accessibility applications
    print("\n2. Accessible Desktop App Trees:")
    atspi_apps = engine.list_apps_from_atspi()
    for app in atspi_apps:
        print(f"  - AT-SPI Registered App: \"{app}\"")

    # 3. Perform Live Coordinate Fusion
    print("\n3. Live Element Coordinate Fusion:")
    # Run coordinate fusion on the active window
    fused = engine.get_clickable_elements()
    if fused:
        print(f"Successfully mapped {len(fused)} sub-elements inside active window!")
        print("\nTop 15 Fused UI Elements (Pixel-Perfect Bounding Boxes):")
        print(f"  {'Index':5} {'Role':15} {'Name/Label':30} {'Absolute Center (X, Y)':25}")
        print("  " + "-" * 78)
        for el in fused[:15]:
            cx, cy = el.center()
            label_display = f"\"{el.label}\"" if el.label else "<anonymous>"
            print(f"  #{el.index:<4} {el.role:15} {label_display[:30]:30} X={cx:<4}, Y={cy:<4} (bounds: {el.bounds})")
    else:
        print("No elements fused. Ensure an accessible application (like Thunar) is focused.")
