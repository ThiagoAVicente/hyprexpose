#include "capture.h"
#include <wayland-client.h>
extern "C" {
#include "hyprland-toplevel-export-v1-client-protocol.h"
}
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

struct CaptureState {
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    bool buffer_done;
    bool frame_ready;
    bool frame_failed;
    uint8_t *data;
    size_t data_size;
    wl_buffer *wl_buf;
};

static void frame_buffer(void *data, struct hyprland_toplevel_export_frame_v1 *,
                         uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    auto *cs = static_cast<CaptureState *>(data);
    cs->format = format;
    cs->width = width;
    cs->height = height;
    cs->stride = stride;
}

static void frame_linux_dmabuf(void *, struct hyprland_toplevel_export_frame_v1 *,
                                uint32_t, uint32_t, uint32_t) {
    // We only use SHM buffers
}

static void frame_buffer_done(void *data, struct hyprland_toplevel_export_frame_v1 *) {
    auto *cs = static_cast<CaptureState *>(data);
    cs->buffer_done = true;
}

static void frame_damage(void *, struct hyprland_toplevel_export_frame_v1 *,
                          uint32_t, uint32_t, uint32_t, uint32_t) {}

static void frame_flags(void *, struct hyprland_toplevel_export_frame_v1 *, uint32_t) {}

static void frame_ready(void *data, struct hyprland_toplevel_export_frame_v1 *,
                        uint32_t, uint32_t, uint32_t) {
    auto *cs = static_cast<CaptureState *>(data);
    cs->frame_ready = true;
}

static void frame_failed(void *data, struct hyprland_toplevel_export_frame_v1 *) {
    auto *cs = static_cast<CaptureState *>(data);
    cs->frame_failed = true;
}

static const struct hyprland_toplevel_export_frame_v1_listener frame_listener = {
    .buffer = frame_buffer,
    .damage = frame_damage,
    .flags = frame_flags,
    .ready = frame_ready,
    .failed = frame_failed,
    .linux_dmabuf = frame_linux_dmabuf,
    .buffer_done = frame_buffer_done,
};

static int create_shm_file(size_t size) {
    char name[] = "/hyprexpose-cap-XXXXXX";
    int fd = memfd_create(name, MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) < 0) { close(fd); return -1; }
    return fd;
}

namespace capture {

Thumbnail capture_toplevel(hyprland_toplevel_export_manager_v1 *manager,
                           wl_shm *shm, wl_display *display,
                           uint64_t window_address) {
    Thumbnail thumb{};
    thumb.address = window_address;
    thumb.ready = false;

    if (!manager) return thumb;

    CaptureState cs{};

    auto *frame = hyprland_toplevel_export_manager_v1_capture_toplevel(
        manager, 0, (uint32_t)window_address);
    hyprland_toplevel_export_frame_v1_add_listener(frame, &frame_listener, &cs);

    // Wait for buffer info
    while (!cs.buffer_done && !cs.frame_failed)
        wl_display_roundtrip(display);

    if (cs.frame_failed || cs.width == 0 || cs.height == 0) {
        hyprland_toplevel_export_frame_v1_destroy(frame);
        return thumb;
    }

    // Create SHM buffer
    cs.data_size = cs.stride * cs.height;
    int fd = create_shm_file(cs.data_size);
    if (fd < 0) {
        hyprland_toplevel_export_frame_v1_destroy(frame);
        return thumb;
    }

    cs.data = static_cast<uint8_t *>(mmap(nullptr, cs.data_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (cs.data == MAP_FAILED) {
        close(fd);
        hyprland_toplevel_export_frame_v1_destroy(frame);
        return thumb;
    }

    auto *pool = wl_shm_create_pool(shm, fd, cs.data_size);
    cs.wl_buf = wl_shm_pool_create_buffer(pool, 0, cs.width, cs.height, cs.stride, cs.format);
    wl_shm_pool_destroy(pool);
    close(fd);

    // Request copy
    hyprland_toplevel_export_frame_v1_copy(frame, cs.wl_buf, 1);

    // Wait for frame
    while (!cs.frame_ready && !cs.frame_failed)
        wl_display_roundtrip(display);

    if (cs.frame_ready) {
        // Copy pixel data to our own allocation
        thumb.data = new uint8_t[cs.data_size];
        memcpy(thumb.data, cs.data, cs.data_size);
        thumb.width = cs.width;
        thumb.height = cs.height;
        thumb.stride = cs.stride;
        thumb.ready = true;
    }

    munmap(cs.data, cs.data_size);
    wl_buffer_destroy(cs.wl_buf);
    hyprland_toplevel_export_frame_v1_destroy(frame);

    return thumb;
}

void free_thumbnail(Thumbnail &t) {
    delete[] t.data;
    t.data = nullptr;
    t.ready = false;
}

} // namespace capture
