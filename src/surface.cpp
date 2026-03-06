#include "surface.h"
#include <wayland-client.h>
// "namespace" is used as a parameter name in the layer-shell protocol header
#define namespace namespace_
extern "C" {
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
}
#undef namespace
extern "C" {
#include "hyprland-toplevel-export-v1-client-protocol.h"
}
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <linux/input-event-codes.h>

// XKB keysyms we care about
#define XKB_KEY_Escape 0xff1b
#define XKB_KEY_Return 0xff0d
#define XKB_KEY_Left   0xff51
#define XKB_KEY_Up     0xff52
#define XKB_KEY_Right  0xff53
#define XKB_KEY_Down   0xff54
#define XKB_KEY_h      0x0068
#define XKB_KEY_j      0x006a
#define XKB_KEY_k      0x006b
#define XKB_KEY_l      0x006c

// Simple keycode to keysym mapping (evdev offset = 8)
static uint32_t keycode_to_keysym(uint32_t keycode) {
    // Common keys - evdev keycodes
    switch (keycode) {
        case KEY_ESC:    return XKB_KEY_Escape;
        case KEY_ENTER:  return XKB_KEY_Return;
        case KEY_LEFT:   return XKB_KEY_Left;
        case KEY_UP:     return XKB_KEY_Up;
        case KEY_RIGHT:  return XKB_KEY_Right;
        case KEY_DOWN:   return XKB_KEY_Down;
        case KEY_H:      return XKB_KEY_h;
        case KEY_J:      return XKB_KEY_j;
        case KEY_K:      return XKB_KEY_k;
        case KEY_L:      return XKB_KEY_l;
        default:         return 0;
    }
}

// Keyboard listener
static void kb_keymap(void *, struct wl_keyboard *, uint32_t, int fd, uint32_t) {
    close(fd);
}
static void kb_enter(void *, struct wl_keyboard *, uint32_t, struct wl_surface *, struct wl_array *) {}
static void kb_leave(void *, struct wl_keyboard *, uint32_t, struct wl_surface *) {}
static void kb_modifiers(void *, struct wl_keyboard *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
static void kb_repeat_info(void *, struct wl_keyboard *, int32_t, int32_t) {}

static void kb_key(void *data, struct wl_keyboard *, uint32_t, uint32_t, uint32_t key, uint32_t state) {
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
    auto *s = static_cast<surface::State *>(data);
    uint32_t keysym = keycode_to_keysym(key);
    if (keysym && s->on_key) {
        s->should_close = s->on_key(keysym);
    }
}

static const struct wl_keyboard_listener kb_listener = {
    .keymap = kb_keymap,
    .enter = kb_enter,
    .leave = kb_leave,
    .key = kb_key,
    .modifiers = kb_modifiers,
    .repeat_info = kb_repeat_info,
};

// Seat listener
static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    auto *s = static_cast<surface::State *>(data);
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !s->keyboard) {
        s->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(s->keyboard, &kb_listener, s);
    }
}
static void seat_name(void *, struct wl_seat *, const char *) {}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

// Layer surface listener
static void layer_configure(void *data, struct zwlr_layer_surface_v1 *lsurf, uint32_t serial, uint32_t w, uint32_t h) {
    auto *s = static_cast<surface::State *>(data);
    s->width = w;
    s->height = h;
    s->configured = true;
    zwlr_layer_surface_v1_ack_configure(lsurf, serial);
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *) {
    auto *s = static_cast<surface::State *>(data);
    s->should_close = true;
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
    .configure = layer_configure,
    .closed = layer_closed,
};

// Registry listener
static void registry_global(void *data, struct wl_registry *reg, uint32_t name, const char *iface, uint32_t version) {
    auto *s = static_cast<surface::State *>(data);
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        s->compositor = static_cast<wl_compositor *>(
            wl_registry_bind(reg, name, &wl_compositor_interface, 4));
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        s->shm = static_cast<wl_shm *>(
            wl_registry_bind(reg, name, &wl_shm_interface, 1));
    } else if (strcmp(iface, wl_seat_interface.name) == 0) {
        s->seat = static_cast<wl_seat *>(
            wl_registry_bind(reg, name, &wl_seat_interface, 7));
        wl_seat_add_listener(s->seat, &seat_listener, s);
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        s->layer_shell = static_cast<zwlr_layer_shell_v1 *>(
            wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, version < 4 ? version : 4));
    } else if (strcmp(iface, hyprland_toplevel_export_manager_v1_interface.name) == 0) {
        s->toplevel_export = static_cast<hyprland_toplevel_export_manager_v1 *>(
            wl_registry_bind(reg, name, &hyprland_toplevel_export_manager_v1_interface, 1));
    }
}

static void registry_global_remove(void *, struct wl_registry *, uint32_t) {}

static const struct wl_registry_listener reg_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

// SHM buffer creation
static int create_shm_file(size_t size) {
    char name[] = "/hyprexpose-XXXXXX";
    int fd = memfd_create(name, MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) < 0) { close(fd); return -1; }
    return fd;
}

namespace surface {

bool init(State &s) {
    s.display = wl_display_connect(nullptr);
    if (!s.display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return false;
    }

    auto *reg = wl_display_get_registry(s.display);
    wl_registry_add_listener(reg, &reg_listener, &s);
    wl_display_roundtrip(s.display);
    wl_display_roundtrip(s.display); // second roundtrip for seat capabilities

    if (!s.compositor || !s.shm || !s.layer_shell) {
        fprintf(stderr, "Missing required Wayland globals\n");
        return false;
    }

    return true;
}

bool create_overlay(State &s) {
    // Just validate we have the globals; actual surface creation happens in show()
    return s.compositor && s.layer_shell && s.shm;
}

void show(State &s) {
    s.should_close = false;
    s.configured = false;

    // Create fresh surface + layer surface each time
    s.wl_surf = wl_compositor_create_surface(s.compositor);
    if (!s.wl_surf) return;

    s.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        s.layer_shell, s.wl_surf, nullptr,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "hyprexpose");

    zwlr_layer_surface_v1_add_listener(s.layer_surface, &layer_listener, &s);
    zwlr_layer_surface_v1_set_anchor(s.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(s.layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(s.layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);

    wl_surface_commit(s.wl_surf);
    wl_display_roundtrip(s.display);

    s.visible = s.configured;
}

void hide(State &s) {
    free_buffer(s);
    if (s.layer_surface) {
        zwlr_layer_surface_v1_destroy(s.layer_surface);
        s.layer_surface = nullptr;
    }
    if (s.wl_surf) {
        wl_surface_destroy(s.wl_surf);
        s.wl_surf = nullptr;
    }
    s.configured = false;
    s.visible = false;
}

void free_buffer(State &s) {
    if (s.buf) { wl_buffer_destroy(s.buf); s.buf = nullptr; }
    if (s.buf_data) { munmap(s.buf_data, s.buf_size); s.buf_data = nullptr; }
    s.buf_size = 0;
    s.buf_stride = 0;
}

uint8_t *get_buffer(State &s, uint32_t w, uint32_t h, uint32_t &stride) {
    stride = w * 4;
    size_t size = (size_t)stride * h;

    // Reuse if same size
    if (s.buf_data && s.buf_size == size) {
        s.buf_stride = stride;
        return s.buf_data;
    }

    free_buffer(s);

    int fd = create_shm_file(size);
    if (fd < 0) return nullptr;

    auto *data = static_cast<uint8_t *>(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (data == MAP_FAILED) { close(fd); return nullptr; }

    auto *pool = wl_shm_create_pool(s.shm, fd, size);
    s.buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    s.buf_data = data;
    s.buf_size = size;
    s.buf_stride = stride;
    return data;
}

void commit(State &s) {
    wl_surface_attach(s.wl_surf, s.buf, 0, 0);
    wl_surface_damage_buffer(s.wl_surf, 0, 0, s.width, s.height);
    wl_surface_commit(s.wl_surf);
}

int dispatch(State &s) {
    return wl_display_dispatch(s.display);
}

int dispatch_pending(State &s) {
    return wl_display_dispatch_pending(s.display);
}

int get_fd(State &s) {
    return wl_display_get_fd(s.display);
}

void flush(State &s) {
    wl_display_flush(s.display);
}

void destroy(State &s) {
    hide(s); // clean up surface + layer surface if visible
    if (s.keyboard) wl_keyboard_destroy(s.keyboard);
    if (s.display) wl_display_disconnect(s.display);
    s = {};
}

} // namespace surface
