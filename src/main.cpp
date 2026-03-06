#include "surface.h"
#include "ipc.h"
#include "capture.h"
#include "render.h"
#include <wayland-client.h>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <cmath>
#include <poll.h>
#include <unistd.h>
#include <vector>

// XKB keysyms (matching surface.cpp)
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

static volatile sig_atomic_t g_toggle = 0;
static volatile sig_atomic_t g_quit = 0;

static void sig_handler(int sig) {
    if (sig == SIGUSR1)
        g_toggle = 1;
    else
        g_quit = 1;
}

struct App {
    surface::State surf;
    std::vector<WorkspaceInfo> workspaces;
    std::vector<Thumbnail> thumbnails;
    int selected = 0;

    void refresh_data() {
        workspaces = ipc::get_workspaces();

        // Free old thumbnails
        for (auto &t : thumbnails)
            capture::free_thumbnail(t);
        thumbnails.clear();

        // Capture thumbnails for all windows
        for (auto &ws : workspaces) {
            for (auto &c : ws.clients) {
                auto thumb = capture::capture_toplevel(
                    surf.toplevel_export, surf.shm, surf.display, c.address);
                if (thumb.ready)
                    thumbnails.push_back(thumb);
            }
        }

        // Set selection to active workspace
        int active = ipc::get_active_workspace();
        selected = 0;
        for (int i = 0; i < (int)workspaces.size(); i++) {
            if (workspaces[i].id == active) {
                selected = i;
                break;
            }
        }
    }

    void free_data() {
        for (auto &t : thumbnails)
            capture::free_thumbnail(t);
        thumbnails.clear();
        workspaces.clear();
    }

    void redraw() {
        if (!surf.configured || surf.width == 0 || surf.height == 0) return;

        uint32_t stride;
        auto *pixels = surface::get_buffer(surf, surf.width, surf.height, stride);
        if (!pixels) return;

        render::RenderContext ctx{pixels, surf.width, surf.height, stride};
        render::draw(ctx, workspaces, selected, thumbnails);
        surface::commit(surf);
    }

    bool handle_key(uint32_t keysym) {
        int n = (int)workspaces.size();
        if (n == 0) return true;

        int cols = std::max(1, (int)std::ceil(std::sqrt(n)));

        switch (keysym) {
            case XKB_KEY_Escape:
                return true;
            case XKB_KEY_Return:
                if (selected >= 0 && selected < n)
                    ipc::switch_workspace(workspaces[selected].id);
                return true;
            case XKB_KEY_Right:
            case XKB_KEY_l:
                selected = std::min(selected + 1, n - 1);
                redraw();
                return false;
            case XKB_KEY_Left:
            case XKB_KEY_h:
                selected = std::max(selected - 1, 0);
                redraw();
                return false;
            case XKB_KEY_Down:
            case XKB_KEY_j:
                if (selected + cols < n) selected += cols;
                redraw();
                return false;
            case XKB_KEY_Up:
            case XKB_KEY_k:
                if (selected - cols >= 0) selected -= cols;
                redraw();
                return false;
        }
        return false;
    }
};

int main() {
    struct sigaction sa{};
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    App app;

    if (!surface::init(app.surf)) return 1;
    if (!surface::create_overlay(app.surf)) {
        fprintf(stderr, "Failed to create overlay surface\n");
        return 1;
    }

    app.surf.on_key = [&app](uint32_t keysym) { return app.handle_key(keysym); };

    fprintf(stderr, "hyprexpose: daemon running (send SIGUSR1 to toggle)\n");

    struct pollfd fds[1];
    fds[0].fd = surface::get_fd(app.surf);
    fds[0].events = POLLIN;

    while (!g_quit) {
        if (g_toggle) {
            g_toggle = 0;
            if (app.surf.visible) {
                surface::hide(app.surf);
                app.free_data();
                surface::flush(app.surf);
            } else {
                surface::show(app.surf);
                app.refresh_data();
                app.redraw();
                surface::flush(app.surf);
            }
        }

        if (app.surf.should_close && app.surf.visible) {
            surface::hide(app.surf);
            app.free_data();
            surface::flush(app.surf);
            app.surf.should_close = false;
        }

        // Block until event or signal
        int ret = poll(fds, 1, -1);
        if (ret < 0) {
            if (errno == EINTR) continue; // signal interrupted
            break;
        }

        if (fds[0].revents & POLLIN) {
            if (wl_display_prepare_read(app.surf.display) == 0) {
                wl_display_read_events(app.surf.display);
                surface::dispatch_pending(app.surf);
            } else {
                surface::dispatch_pending(app.surf);
            }

            if (app.surf.should_close && app.surf.visible) {
                surface::hide(app.surf);
                surface::flush(app.surf);
                app.surf.should_close = false;
            }
        }

        surface::flush(app.surf);
    }

    app.free_data();
    surface::destroy(app.surf);

    return 0;
}
