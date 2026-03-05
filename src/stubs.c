// Stub interface definitions for protocol features we don't use
#include <wayland-client.h>

// Referenced by wlr-layer-shell (get_popup request) but we never call it
const struct wl_interface xdg_popup_interface = {
    "xdg_popup", 1, 0, NULL, 0, NULL,
};

// Referenced by hyprland-toplevel-export v2 (capture_toplevel_with_wlr_toplevel_handle)
const struct wl_interface zwlr_foreign_toplevel_handle_v1_interface = {
    "zwlr_foreign_toplevel_handle_v1", 1, 0, NULL, 0, NULL,
};
