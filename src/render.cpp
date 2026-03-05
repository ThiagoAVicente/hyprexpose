#include "render.h"
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <cmath>
#include <algorithm>

static const double BG_ALPHA = 0.75;
static const double CARD_RADIUS = 12.0;
static const double CARD_PAD = 24.0;
static const double LABEL_HEIGHT = 32.0;
static const double THUMB_PAD = 8.0;
static const double SELECT_BORDER = 3.0;

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

// Find thumbnail for a given window address
static const Thumbnail *find_thumb(const std::vector<Thumbnail> &thumbs, uint64_t addr) {
    for (auto &t : thumbs)
        if (t.address == addr && t.ready)
            return &t;
    return nullptr;
}

namespace render {

void draw(RenderContext &ctx, const std::vector<WorkspaceInfo> &workspaces,
          int selected_index, const std::vector<Thumbnail> &thumbnails) {

    auto *surface = cairo_image_surface_create_for_data(
        ctx.pixels, CAIRO_FORMAT_ARGB32, ctx.width, ctx.height, ctx.stride);
    auto *cr = cairo_create(surface);

    // Dimmed background
    cairo_set_source_rgba(cr, 0, 0, 0, BG_ALPHA);
    cairo_paint(cr);

    if (workspaces.empty()) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return;
    }

    // Layout: compute card dimensions
    int n = (int)workspaces.size();
    int cols = std::min(n, std::max(1, (int)std::ceil(std::sqrt(n))));
    int rows = (n + cols - 1) / cols;

    double avail_w = ctx.width - CARD_PAD * (cols + 1);
    double avail_h = ctx.height - CARD_PAD * (rows + 1);
    double card_w = avail_w / cols;
    double card_h = avail_h / rows;

    // Cap card size to reasonable max
    double max_card_w = 480.0;
    double max_card_h = 320.0;
    card_w = std::min(card_w, max_card_w);
    card_h = std::min(card_h, max_card_h);

    // Center the grid
    double grid_w = cols * card_w + (cols - 1) * CARD_PAD;
    double grid_h = rows * card_h + (rows - 1) * CARD_PAD;
    double ox = (ctx.width - grid_w) / 2.0;
    double oy = (ctx.height - grid_h) / 2.0;

    PangoFontDescription *font = pango_font_description_from_string("Sans 11");
    PangoFontDescription *label_font = pango_font_description_from_string("Sans Bold 13");

    for (int i = 0; i < n; i++) {
        int col = i % cols;
        int row = i / cols;
        double cx = ox + col * (card_w + CARD_PAD);
        double cy = oy + row * (card_h + CARD_PAD);

        // Selection highlight
        if (i == selected_index) {
            rounded_rect(cr, cx - SELECT_BORDER, cy - SELECT_BORDER,
                        card_w + 2 * SELECT_BORDER, card_h + 2 * SELECT_BORDER, CARD_RADIUS + 2);
            cairo_set_source_rgba(cr, 0.4, 0.6, 1.0, 0.9);
            cairo_fill(cr);
        }

        // Card background
        rounded_rect(cr, cx, cy, card_w, card_h, CARD_RADIUS);
        cairo_set_source_rgba(cr, 0.12, 0.12, 0.15, 0.95);
        cairo_fill(cr);

        // Workspace label
        {
            auto *layout = pango_cairo_create_layout(cr);
            std::string label = std::to_string(workspaces[i].id);
            if (!workspaces[i].name.empty() && workspaces[i].name != label)
                label += " " + workspaces[i].name;
            pango_layout_set_text(layout, label.c_str(), -1);
            pango_layout_set_font_description(layout, label_font);

            int tw, th;
            pango_layout_get_pixel_size(layout, &tw, &th);

            cairo_set_source_rgba(cr, 0.85, 0.85, 0.9, 1.0);
            cairo_move_to(cr, cx + (card_w - tw) / 2, cy + 6);
            pango_cairo_show_layout(cr, layout);
            g_object_unref(layout);
        }

        // Window area
        double win_y = cy + LABEL_HEIGHT;
        double win_h = card_h - LABEL_HEIGHT - THUMB_PAD;
        double win_w = card_w - 2 * THUMB_PAD;
        double win_x = cx + THUMB_PAD;

        auto &clients = workspaces[i].clients;
        if (clients.empty()) {
            // Empty workspace indicator
            auto *layout = pango_cairo_create_layout(cr);
            pango_layout_set_text(layout, "(empty)", -1);
            pango_layout_set_font_description(layout, font);
            int tw, th;
            pango_layout_get_pixel_size(layout, &tw, &th);
            cairo_set_source_rgba(cr, 0.5, 0.5, 0.55, 0.8);
            cairo_move_to(cr, cx + (card_w - tw) / 2, win_y + (win_h - th) / 2);
            pango_cairo_show_layout(cr, layout);
            g_object_unref(layout);
            continue;
        }

        // Find workspace bounds for scaling
        int min_x = INT32_MAX, min_y = INT32_MAX, max_x = 0, max_y = 0;
        for (auto &c : clients) {
            min_x = std::min(min_x, c.x);
            min_y = std::min(min_y, c.y);
            max_x = std::max(max_x, c.x + c.w);
            max_y = std::max(max_y, c.y + c.h);
        }

        double ws_w = std::max(1, max_x - min_x);
        double ws_h = std::max(1, max_y - min_y);
        double scale = std::min(win_w / ws_w, win_h / ws_h) * 0.9;

        // Center the layout
        double scaled_w = ws_w * scale;
        double scaled_h = ws_h * scale;
        double off_x = win_x + (win_w - scaled_w) / 2;
        double off_y = win_y + (win_h - scaled_h) / 2;

        for (auto &c : clients) {
            double rx = off_x + (c.x - min_x) * scale;
            double ry = off_y + (c.y - min_y) * scale;
            double rw = c.w * scale;
            double rh = c.h * scale;

            const Thumbnail *thumb = find_thumb(thumbnails, c.address);
            if (thumb && thumb->data) {
                // Draw actual thumbnail
                auto *img = cairo_image_surface_create_for_data(
                    thumb->data, CAIRO_FORMAT_ARGB32,
                    thumb->width, thumb->height, thumb->stride);

                cairo_save(cr);
                rounded_rect(cr, rx, ry, rw, rh, 4);
                cairo_clip(cr);

                double sx = rw / thumb->width;
                double sy = rh / thumb->height;
                cairo_translate(cr, rx, ry);
                cairo_scale(cr, sx, sy);
                cairo_set_source_surface(cr, img, 0, 0);
                cairo_paint(cr);
                cairo_restore(cr);
                cairo_surface_destroy(img);
            } else {
                // Colored rectangle placeholder
                // Hash class name for consistent color
                uint32_t hash = 0;
                for (char ch : c.class_name) hash = hash * 31 + ch;
                double r = 0.2 + (hash % 100) / 200.0;
                double g = 0.2 + ((hash / 100) % 100) / 200.0;
                double b = 0.3 + ((hash / 10000) % 100) / 200.0;

                rounded_rect(cr, rx, ry, rw, rh, 4);
                cairo_set_source_rgba(cr, r, g, b, 0.85);
                cairo_fill(cr);
            }

            // Window class label
            if (rw > 40 && rh > 20) {
                auto *layout = pango_cairo_create_layout(cr);
                std::string name = c.class_name.empty() ? c.title : c.class_name;
                pango_layout_set_text(layout, name.c_str(), -1);
                pango_layout_set_font_description(layout, font);
                pango_layout_set_width(layout, (int)(rw - 4) * PANGO_SCALE);
                pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

                int tw, th;
                pango_layout_get_pixel_size(layout, &tw, &th);

                cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
                cairo_move_to(cr, rx + (rw - tw) / 2, ry + (rh - th) / 2);
                pango_cairo_show_layout(cr, layout);
                g_object_unref(layout);
            }
        }
    }

    pango_font_description_free(font);
    pango_font_description_free(label_font);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

} // namespace render
