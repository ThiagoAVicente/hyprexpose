#include "ipc.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <algorithm>

// Minimal JSON parsing — we only need flat arrays of objects with known keys.
// Avoids pulling in a JSON library for this simple use case.

static std::string hypr_socket_path() {
    const char *xdg = std::getenv("XDG_RUNTIME_DIR");
    const char *sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!xdg || !sig)
        throw std::runtime_error("HYPRLAND_INSTANCE_SIGNATURE or XDG_RUNTIME_DIR not set");
    return std::string(xdg) + "/hypr/" + sig + "/.socket.sock";
}

static std::string hypr_request(const std::string &cmd) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("socket() failed");

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string path = hypr_socket_path();
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        throw std::runtime_error("connect() to hyprland socket failed");
    }

    // Send command
    std::string full = "j/" + cmd; // j/ prefix for JSON output
    write(fd, full.c_str(), full.size());

    // Read response
    std::string resp;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        resp.append(buf, n);

    close(fd);
    return resp;
}

// Simple JSON helpers — no full parser needed for Hyprland's flat JSON
static std::string json_string(const std::string &json, const std::string &key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    if (pos < json.size() && json[pos] == '"') {
        pos++;
        std::string val;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++;
            }
            val += json[pos++];
        }
        return val;
    }
    return "";
}

static int64_t json_int(const std::string &json, const std::string &key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    std::string num;
    while (pos < json.size() && (json[pos] == '-' || (json[pos] >= '0' && json[pos] <= '9')))
        num += json[pos++];
    return num.empty() ? 0 : std::stoll(num);
}

// Split JSON array into individual objects (top-level only)
static std::vector<std::string> json_array_objects(const std::string &json) {
    std::vector<std::string> objs;
    int depth = 0;
    size_t start = 0;
    bool in_string = false;

    for (size_t i = 0; i < json.size(); i++) {
        char c = json[i];
        if (c == '"' && (i == 0 || json[i - 1] != '\\'))
            in_string = !in_string;
        if (in_string) continue;

        if (c == '{') {
            if (depth == 0) start = i;
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0)
                objs.push_back(json.substr(start, i - start + 1));
        }
    }
    return objs;
}

// Parse "address":"0xdeadbeef" -> uint64_t
static uint64_t parse_address(const std::string &json) {
    std::string addr_str = json_string(json, "address");
    if (addr_str.empty()) return 0;
    // Remove 0x prefix if present
    if (addr_str.size() > 2 && addr_str[0] == '0' && addr_str[1] == 'x')
        addr_str = addr_str.substr(2);
    return std::stoull(addr_str, nullptr, 16);
}

namespace ipc {

std::vector<WorkspaceInfo> get_workspaces() {
    std::string ws_json = hypr_request("workspaces");
    std::string cl_json = hypr_request("clients");

    auto ws_objs = json_array_objects(ws_json);
    auto cl_objs = json_array_objects(cl_json);

    std::vector<WorkspaceInfo> workspaces;
    for (auto &wj : ws_objs) {
        WorkspaceInfo ws;
        ws.id = (int)json_int(wj, "id");
        ws.name = json_string(wj, "name");
        ws.monitor_id = (int)json_int(wj, "monitorID");
        if (ws.id < 1) continue; // skip special workspaces
        workspaces.push_back(ws);
    }

    // Sort by ID
    std::sort(workspaces.begin(), workspaces.end(),
              [](const WorkspaceInfo &a, const WorkspaceInfo &b) { return a.id < b.id; });

    // Assign clients to workspaces
    for (auto &cj : cl_objs) {
        ClientInfo ci;
        ci.class_name = json_string(cj, "class");
        ci.title = json_string(cj, "title");
        ci.address = parse_address(cj);
        ci.workspace_id = (int)json_int(cj, "workspace");

        // Parse position "at":[x,y] — Hyprland uses "at" as array
        // But in JSON it looks like "at":[x,y] - need to parse array
        {
            auto pos = cj.find("\"at\":");
            if (pos != std::string::npos) {
                pos = cj.find('[', pos);
                if (pos != std::string::npos) {
                    pos++;
                    ci.x = std::stoi(cj.substr(pos));
                    pos = cj.find(',', pos);
                    if (pos != std::string::npos)
                        ci.y = std::stoi(cj.substr(pos + 1));
                }
            }
        }
        // Parse size "size":[w,h]
        {
            auto pos = cj.find("\"size\":");
            if (pos != std::string::npos) {
                pos = cj.find('[', pos);
                if (pos != std::string::npos) {
                    pos++;
                    ci.w = std::stoi(cj.substr(pos));
                    pos = cj.find(',', pos);
                    if (pos != std::string::npos)
                        ci.h = std::stoi(cj.substr(pos + 1));
                }
            }
        }

        // Workspace ID might also be nested: "workspace":{"id":1,"name":"1"}
        // Try nested format
        if (ci.workspace_id == 0) {
            auto wpos = cj.find("\"workspace\":");
            if (wpos != std::string::npos) {
                auto brace = cj.find('{', wpos);
                if (brace != std::string::npos) {
                    auto end = cj.find('}', brace);
                    if (end != std::string::npos) {
                        std::string inner = cj.substr(brace, end - brace + 1);
                        ci.workspace_id = (int)json_int(inner, "id");
                    }
                }
            }
        }

        for (auto &ws : workspaces) {
            if (ws.id == ci.workspace_id) {
                ws.clients.push_back(ci);
                break;
            }
        }
    }

    return workspaces;
}

void switch_workspace(int id) {
    // Use the dispatch socket (socket2)
    const char *xdg = std::getenv("XDG_RUNTIME_DIR");
    const char *sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!xdg || !sig) return;

    std::string path = std::string(xdg) + "/hypr/" + sig + "/.socket.sock";
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return;
    }

    std::string cmd = "/dispatch workspace " + std::to_string(id);
    write(fd, cmd.c_str(), cmd.size());

    char buf[256];
    read(fd, buf, sizeof(buf)); // read response
    close(fd);
}

int get_active_workspace() {
    std::string json = hypr_request("activeworkspace");
    return (int)json_int(json, "id");
}

} // namespace ipc
