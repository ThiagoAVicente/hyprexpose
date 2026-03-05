#pragma once
#include <cstdint>
#include <functional>

struct wl_display;
struct wl_surface;
struct wl_shm;
struct wl_buffer;
struct wl_compositor;
struct wl_seat;
struct wl_keyboard;
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;
struct hyprland_toplevel_export_manager_v1;

// Key event callback: keycode (XKB), return true to close overlay
using KeyCallback = std::function<bool(uint32_t keysym)>;

namespace surface {

struct State {
    wl_display *display = nullptr;
    wl_compositor *compositor = nullptr;
    wl_shm *shm = nullptr;
    wl_seat *seat = nullptr;
    wl_keyboard *keyboard = nullptr;
    wl_surface *wl_surf = nullptr;
    zwlr_layer_shell_v1 *layer_shell = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    hyprland_toplevel_export_manager_v1 *toplevel_export = nullptr;

    uint32_t width = 0;
    uint32_t height = 0;
    bool configured = false;
    bool visible = false;
    bool should_close = false;

    KeyCallback on_key;
};

// Connect to wayland display & bind globals
bool init(State &s);

// Create the layer-shell overlay surface (initially unmapped)
bool create_overlay(State &s);

// Show the overlay (attach buffer, commit)
void show(State &s);

// Hide the overlay (attach null buffer)
void hide(State &s);

// Create a SHM buffer for rendering, returns pixel data pointer
uint8_t *create_buffer(State &s, uint32_t w, uint32_t h, uint32_t &stride, struct wl_buffer **buf_out);

// Attach buffer and commit surface
void commit(State &s, struct wl_buffer *buf);

// Dispatch wayland events (blocking)
int dispatch(State &s);

// Dispatch pending events (non-blocking)
int dispatch_pending(State &s);

// Get the display fd for poll()
int get_fd(State &s);

// Flush the display
void flush(State &s);

// Cleanup
void destroy(State &s);

} // namespace surface
