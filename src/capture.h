#pragma once
#include <cstdint>
#include <vector>
#include <functional>

struct wl_shm;
struct hyprland_toplevel_export_manager_v1;
struct wl_display;

struct Thumbnail {
    uint64_t address;     // window address
    uint8_t *data;        // ARGB pixel data (owned by capture)
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    bool ready;
};

namespace capture {

// Capture a single toplevel window's frame
// Returns a Thumbnail (check .ready field)
Thumbnail capture_toplevel(hyprland_toplevel_export_manager_v1 *manager,
                           wl_shm *shm, wl_display *display,
                           uint64_t window_address);

// Free thumbnail pixel data
void free_thumbnail(Thumbnail &t);

} // namespace capture
