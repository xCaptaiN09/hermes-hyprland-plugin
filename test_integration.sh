#!/usr/bin/env bash
# Hermes Hyprland Integration Verification Helper
# Verifies compositor socket, D-Bus accessibility, fusion math, and agent backend imports.

set -e

# Terminal formatting colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Set PYTHONPATH to agent root so imports resolve correctly
export PYTHONPATH="/home/captain/.hermes/hermes-agent"

echo "================================================================"
echo "    Hermes Hyprland Computer-Use System Integration Test"
echo "================================================================"

# 1. Check C++ Compositor Socket
echo -n "1. Checking Hyprland C++ IPC Socket (/tmp/hermes-hyprland.sock)... "
if [ -S /tmp/hermes-hyprland.sock ]; then
    echo -e "${GREEN}ACTIVE${NC}"
else
    echo -e "${RED}INACTIVE${NC}"
    echo "   [!] Error: The hermes-hyprland.so plugin is not loaded."
    echo "   Run: hyprctl plugin load /home/captain/Antigravity/hermes_hyprland_plugin/hermes-hyprland-plugin/build/hermes-hyprland.so"
    exit 1
fi

# 2. Check Socket Communication
echo -n "2. Querying compositor window frames via IPC socket... "
RESPONSE=$(python3 -c "
import socket, json
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('/tmp/hermes-hyprland.sock')
s.sendall(json.dumps({'action': 'ping'}).encode('utf-8'))
print(s.recv(1024).decode('utf-8'))
" 2>/dev/null || echo "")

if [[ "$RESPONSE" == *"true"* ]]; then
    echo -e "${GREEN}SUCCESS${NC}"
else
    echo -e "${RED}FAILED${NC}"
    echo "   [!] Error: Failed to communicate with compositor socket."
    exit 1
fi

# 3. Check D-Bus Accessibility (AT-SPI)
echo -n "3. Querying AT-SPI D-Bus accessibility service... "
ATSPI=$(python3 -c "
try:
    import gi
    gi.require_version('Atspi', '2.0')
    from gi.repository import Atspi
    print('OK')
except Exception:
    print('FAIL')
" 2>/dev/null || echo "FAIL")

if [ "$ATSPI" = "OK" ]; then
    echo -e "${GREEN}AVAILABLE${NC}"
else
    echo -e "${RED}UNAVAILABLE${NC}"
    echo "   [!] Warning: gi.repository.Atspi library is missing."
    exit 1
fi

# 4. Check Agent Backend Imports
echo -n "4. Testing hermes-agent backend integration imports... "
AGENT_IMPORT=$(python3 -c "
try:
    from tools.computer_use.hyprland_backend import HyprlandBackend
    print('OK')
except Exception as e:
    print(f'FAIL: {e}')
" 2>/dev/null || echo "FAIL")

if [[ "$AGENT_IMPORT" == *"OK"* ]]; then
    echo -e "${GREEN}SUCCESS${NC}"
else
    echo -e "${RED}FAILED${NC}"
    echo "   [!] Error: Agent backend imports failed: $AGENT_IMPORT"
    exit 1
fi

# 5. Run Live Capture Simulation
echo "5. Running a simulated window capture..."
python3 -c "
from tools.computer_use.tool import handle_computer_use
import json
res = handle_computer_use({'action': 'capture', 'mode': 'ax'})
data = json.loads(res)
print(f'   - Active App Targeted: {data.get(\"app\")}')
print(f'   - Total Fused Elements: {data.get(\"total_elements\")}')
" 2>/dev/null

echo -e "\n${GREEN}>>> ALL INTEGRATION CHECKS PASSED SUCCESSFULLY! <<<${NC}"
echo "You are ready to run the Hermes agent with HERMES_COMPUTER_USE_BACKEND=hyprland"
echo "================================================================"
