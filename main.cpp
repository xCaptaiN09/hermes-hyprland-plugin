#define WLR_USE_UNSTABLE
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <thread>
#include <string>
#include <sstream>
#include <vector>
#include <cstring>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static HANDLE PHANDLE = nullptr;


APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

#define SOCKET_PATH "/tmp/hermes-hyprland.sock"

static int server_fd = -1;
static std::thread socket_thread;
static bool running = false;

json get_windows() {
    json windows = json::array();
    int idx = 1;
    for (auto& window : g_pCompositor->m_windows) {
        if (!window->m_isMapped) continue;
        json w;
        w["index"] = idx++;
        w["address"] = (std::string)std::to_string((uintptr_t)window.get());
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

        if (action == "get_windows") {
            response["success"] = true;
            response["windows"] = get_windows();
        }
        else if (action == "get_cursor") {
            response["success"] = true;
            response["cursor"] = get_cursor();
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
                response["height"] = (int)win->m_realSize->value().y;
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
        else if (action == "ping") {
            response["success"] = true;
            response["message"] = "hermes-hyprland plugin active";
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

        char buf[4096] = {0};
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
        "[hermes] Plugin loaded — socket at " SOCKET_PATH,
        CHyprColor{0.2, 0.9, 0.2, 1.0}, 5000);

    return {"hermes-hyprland", "Hermes Agent compositor control", "CaptaiN", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    running = false;
    if (server_fd >= 0) {
        close(server_fd);
        unlink(SOCKET_PATH);
    }
}
