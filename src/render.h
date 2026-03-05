#pragma once
#include <cstdint>
#include <vector>
#include "ipc.h"
#include "capture.h"

namespace render {

struct RenderContext {
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
};

// Render the workspace overview into the buffer
// selected_index: currently highlighted workspace (-1 for none)
// thumbnails: captured window thumbnails (may be empty for placeholder mode)
void draw(RenderContext &ctx,
          const std::vector<WorkspaceInfo> &workspaces,
          int selected_index,
          const std::vector<Thumbnail> &thumbnails);

} // namespace render
