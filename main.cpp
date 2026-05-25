#define WLR_USE_UNSTABLE
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/view/Subsurface.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/protocols/XDGShell.hpp>
#include <hyprland/src/protocols/core/Subcompositor.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/protocols/types/SurfaceState.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <thread>
#include <string>
#include <vector>
#include <cstring>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static HANDLE PHANDLE = nullptr;
#define SOCKET_PATH "/tmp/hermes-hyprland.sock"

static int server_fd = -1;
static std::thread socket_thread;
static bool running = false;

// Recursively collect all clickable surface regions for a window
void collectSurfaceRegions(SP<CWLSurfaceResource> surface, json& regions, int& idx) {
    if (!surface) return;

    auto wlSurface = Desktop::View::CWLSurface::fromResource(surface);
    if (wlSurface) {
        auto globalBox = wlSurface->getSurfaceBoxGlobal();
        if (globalBox.has_value()) {
            auto gb = globalBox.value();
            auto inputExtents = surface->m_current.input.getExtents();
            bool isInfinite = (inputExtents.w) > 10000;

            json region;
            region["index"] = idx++;
            if (!isInfinite) {
                region["x"] = (int)(gb.x + inputExtents.x);
                region["y"] = (int)(gb.y + inputExtents.y);
                region["width"] = (int)(inputExtents.w);
                region["height"] = (int)(inputExtents.h);
            } else {
                region["x"] = (int)gb.x;
                region["y"] = (int)gb.y;
                region["width"] = (int)gb.w;
                region["height"] = (int)gb.h;
            }
            region["center_x"] = region["x"].get<int>() + region["width"].get<int>() / 2;
            region["center_y"] = region["y"].get<int>() + region["height"].get<int>() / 2;
            region["type"] = isInfinite ? "surface" : "input_region";
            regions.push_back(region);
        }
    }

    for (auto& sub : surface->m_subsurfaces) {
        auto subRes = sub.lock();
        if (!subRes) continue;
        // get surface from subsurface resource
        auto subSurface = subRes->m_surface.lock();
        if (subSurface) collectSurfaceRegions(subSurface, regions, idx);
    }
}

json get_windows() {
    json windows = json::array();
    int idx = 1;
    for (auto& window : g_pCompositor->m_windows) {
        if (!window->m_isMapped) continue;
        json w;
        w["index"] = idx++;
        w["address"] = std::to_string((uintptr_t)window.get());
        w["class"] = window->m_class;
        w["title"] = window->m_title;
        w["x"] = (int)window->m_realPosition->value().x;
        w["y"] = (int)window->m_realPosition->value().y;
        w["width"] = (int)window->m_realSize->value().x;
        w["height"] = (int)window->m_realSize->value().y;
        w["center_x"] = (int)(window->m_realPosition->value().x + window->m_realSize->value().x / 2);
        w["center_y"] = (int)(window->m_realPosition->value().y + window->m_realSize->value().y / 2);
        w["workspace"] = window->workspaceID();
        w["focused"] = (window == Desktop::focusState()->window());
        windows.push_back(w);
    }
    return windows;
}

json get_clickable_regions(const std::string& filter_class = "") {
    json result;
    result["windows"] = json::array();
    int regionIdx = 1;

    for (auto& window : g_pCompositor->m_windows) {
        if (!window->m_isMapped) continue;
        if (!filter_class.empty() && window->m_class.find(filter_class) == std::string::npos) continue;

        json winfo;
        winfo["class"] = window->m_class;
        winfo["title"] = window->m_title;
        winfo["x"] = (int)window->m_realPosition->value().x;
        winfo["y"] = (int)window->m_realPosition->value().y;
        winfo["width"] = (int)window->m_realSize->value().x;
        winfo["height"] = (int)window->m_realSize->value().y;
        winfo["regions"] = json::array();

        // Get the XDG surface resource
        auto xdgSurface = window->m_xdgSurface.lock();
        if (xdgSurface) {
            auto surface = xdgSurface->m_surface.lock();
            if (surface) {
                Vector2D winPos = {window->m_realPosition->value().x, window->m_realPosition->value().y};
                collectSurfaceRegions(surface, winfo["regions"], regionIdx);
            }
        }

        result["windows"].push_back(winfo);
    }
    return result;
}

json get_cursor() {
    auto pos = g_pInputManager->getMouseCoordsInternal();
    json j;
    j["x"] = (int)pos.x;
    j["y"] = (int)pos.y;
    return j;
}

std::string handle_command(const std::string& cmd) {
    json response;
    try {
        json req = json::parse(cmd);
        std::string action = req["action"];

        if (action == "ping") {
            response["success"] = true;
            response["message"] = "hermes-hyprland plugin v2 active - compositor element map enabled";
        }
        else if (action == "get_windows") {
            response["success"] = true;
            response["windows"] = get_windows();
        }
        else if (action == "get_cursor") {
            response["success"] = true;
            response["cursor"] = get_cursor();
        }
        else if (action == "get_clickable_regions") {
            std::string filter = req.value("class", "");
            response["success"] = true;
            response = get_clickable_regions(filter);
            response["success"] = true;
        }
        else if (action == "get_active") {
            auto win = Desktop::focusState()->window();
            if (win) {
                response["success"] = true;
                response["class"] = win->m_class;
                response["title"] = win->m_title;
                response["x"] = (int)win->m_realPosition->value().x;
                response["y"] = (int)win->m_realPosition->value().y;
                response["width"] = (int)win->m_realSize->value().x;
                response["height"] = win->m_realSize->value().y;
            } else {
                response["success"] = false;
                response["error"] = "no active window";
            }
        }
        else if (action == "move_cursor") {
            int x = req["x"];
            int y = req["y"];
            g_pCompositor->warpCursorTo({(double)x, (double)y}, true);
            response["success"] = true;
            response["x"] = x;
            response["y"] = y;
        }
        else if (action == "virtual_click") {
            int x = req["x"];
            int y = req["y"];
            
            // 1. Record original cursor position
            auto originalPos = g_pInputManager->getMouseCoordsInternal();
            
            // 2. Warp pointer to target
            g_pCompositor->warpCursorTo({(double)x, (double)y}, true);
            
            // 3. Trigger native click
            if (!g_pInputManager->m_pointers.empty()) {
                auto pointer = g_pInputManager->m_pointers.front();
                
                IPointer::SButtonEvent pressEvent;
                pressEvent.button = 272; // BTN_LEFT
                pressEvent.state = WL_POINTER_BUTTON_STATE_PRESSED;
                pressEvent.mouse = true;
                g_pInputManager->onMouseButton(pressEvent, pointer);

                IPointer::SButtonEvent releaseEvent;
                releaseEvent.button = 272; // BTN_LEFT
                releaseEvent.state = WL_POINTER_BUTTON_STATE_RELEASED;
                releaseEvent.mouse = true;
                g_pInputManager->onMouseButton(releaseEvent, pointer);
            }
            
            // 4. Warp pointer back to original position
            g_pCompositor->warpCursorTo(originalPos, true);
            
            response["success"] = true;
            response["x"] = x;
            response["y"] = y;
        }
        else if (action == "focus_window") {
            std::string cls = req.value("class", "");
            std::string title = req.value("title", "");
            for (auto& window : g_pCompositor->m_windows) {
                if (!window->m_isMapped) continue;
                if ((!cls.empty() && window->m_class.find(cls) != std::string::npos) ||
                    (!title.empty() && window->m_title.find(title) != std::string::npos)) {
                    Desktop::focusState()->fullWindowFocus(window, Desktop::FOCUS_REASON_OTHER);
                    response["success"] = true;
                    response["focused"] = window->m_title;
                    break;
                }
            }
            if (!response.contains("success")) {
                response["success"] = false;
                response["error"] = "window not found";
            }
        }
        else {
            response["success"] = false;
            response["error"] = "unknown action: " + action;
        }
    } catch (const std::exception& e) {
        response["success"] = false;
        response["error"] = e.what();
    }
    return response.dump();
}

void socket_server() {
    struct sockaddr_un addr;
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) return;

    unlink(SOCKET_PATH);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) return;
    listen(server_fd, 5);

    while (running) {
        int client = accept(server_fd, nullptr, nullptr);
        if (client < 0) continue;

        char buf[65536] = {0};
        int n = read(client, buf, sizeof(buf) - 1);
        if (n > 0) {
            std::string response = handle_command(std::string(buf, n));
            write(client, response.c_str(), response.size());
        }
        close(client);
    }
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;
    running = true;
    socket_thread = std::thread(socket_server);
    socket_thread.detach();

    HyprlandAPI::addNotification(PHANDLE,
        "[hermes] v2 loaded — compositor element map active",
        CHyprColor{0.2, 0.9, 0.2, 1.0}, 5000);

    return {"hermes-hyprland", "Hermes Agent compositor control v2", "CaptaiN", "2.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    running = false;
    if (server_fd >= 0) {
        close(server_fd);
        unlink(SOCKET_PATH);
    }
}
