#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct ClientInfo {
    std::string class_name;
    std::string title;
    uint64_t address;
    int workspace_id;
    int x, y, w, h; // position and size on the workspace
};

struct WorkspaceInfo {
    int id;
    std::string name;
    int monitor_id;
    std::vector<ClientInfo> clients;
};

namespace ipc {

// Query active workspaces and their clients from Hyprland
std::vector<WorkspaceInfo> get_workspaces();

// Switch to workspace by ID
void switch_workspace(int id);

// Get the active workspace ID
int get_active_workspace();

} // namespace ipc
