use crate::ipc::{ClientInfo, WorkspaceInfo};
use cairo::{Context, Format, ImageSurface};
use pango::FontDescription;

pub struct Thumbnail {
    pub address: u64,
    pub data: Vec<u8>,
    pub width: u32,
    pub height: u32,
    pub stride: u32,
}

const BG_ALPHA: f64 = 0.75;
const CARD_RADIUS: f64 = 12.0;
const CARD_PAD: f64 = 24.0;
const LABEL_HEIGHT: f64 = 32.0;
const THUMB_PAD: f64 = 8.0;
const SELECT_BORDER: f64 = 3.0;

fn rounded_rect(cr: &Context, x: f64, y: f64, w: f64, h: f64, r: f64) {
    use std::f64::consts::PI;
    cr.new_sub_path();
    cr.arc(x + w - r, y + r, r, -PI / 2.0, 0.0);
    cr.arc(x + w - r, y + h - r, r, 0.0, PI / 2.0);
    cr.arc(x + r, y + h - r, r, PI / 2.0, PI);
    cr.arc(x + r, y + r, r, PI, 3.0 * PI / 2.0);
    cr.close_path();
}

fn find_thumb<'a>(thumbnails: &'a [Thumbnail], address: u64) -> Option<&'a Thumbnail> {
    thumbnails.iter().find(|t| t.address == address)
}

fn draw_window_label(cr: &Context, font: &FontDescription, text: &str, rx: f64, ry: f64, rw: f64, rh: f64) {
    let layout = pangocairo::functions::create_layout(cr);
    layout.set_text(text);
    layout.set_font_description(Some(font));
    layout.set_width(((rw - 4.0) * pango::SCALE as f64) as i32);
    layout.set_ellipsize(pango::EllipsizeMode::End);
    let (tw, th) = layout.pixel_size();
    cr.set_source_rgba(1.0, 1.0, 1.0, 0.9);
    cr.move_to(rx + (rw - tw as f64) / 2.0, ry + (rh - th as f64) / 2.0);
    pangocairo::functions::show_layout(cr, &layout);
}

fn draw_client(
    cr: &Context,
    font: &FontDescription,
    client: &ClientInfo,
    min_x: i32,
    min_y: i32,
    scale: f64,
    off_x: f64,
    off_y: f64,
    thumbnails: &[Thumbnail],
) {
    let rx = off_x + (client.x - min_x) as f64 * scale;
    let ry = off_y + (client.y - min_y) as f64 * scale;
    let rw = client.w as f64 * scale;
    let rh = client.h as f64 * scale;

    if let Some(thumb) = find_thumb(thumbnails, client.address) {
        if let Ok(img) = ImageSurface::create_for_data(
            thumb.data.clone(),
            Format::ARgb32,
            thumb.width as i32,
            thumb.height as i32,
            thumb.stride as i32,
        ) {
            cr.save().ok();
            rounded_rect(cr, rx, ry, rw, rh, 4.0);
            cr.clip();
            let sx = rw / thumb.width as f64;
            let sy = rh / thumb.height as f64;
            cr.translate(rx, ry);
            cr.scale(sx, sy);
            cr.set_source_surface(&img, 0.0, 0.0).ok();
            cr.paint().ok();
            cr.restore().ok();
        }
    } else {
        // Hash class name for a consistent placeholder color
        let hash: u32 = client
            .class_name
            .bytes()
            .fold(0u32, |h, b| h.wrapping_mul(31).wrapping_add(b as u32));
        let r = 0.2 + (hash % 100) as f64 / 200.0;
        let g = 0.2 + ((hash / 100) % 100) as f64 / 200.0;
        let b = 0.3 + ((hash / 10000) % 100) as f64 / 200.0;
        rounded_rect(cr, rx, ry, rw, rh, 4.0);
        cr.set_source_rgba(r, g, b, 0.85);
        cr.fill().ok();
    }

    if rw > 40.0 && rh > 20.0 {
        let name = if client.class_name.is_empty() {
            &client.title
        } else {
            &client.class_name
        };
        draw_window_label(cr, font, name, rx, ry, rw, rh);
    }
}

pub fn draw(
    width: u32,
    height: u32,
    workspaces: &[WorkspaceInfo],
    selected_index: usize,
    thumbnails: &[Thumbnail],
) -> Vec<u8> {
    let stride = width * 4;
    let size = (stride * height) as usize;
    let buf = vec![0u8; size];

    let surface =
        match ImageSurface::create_for_data(buf, Format::ARgb32, width as i32, height as i32, stride as i32) {
            Ok(s) => s,
            Err(_) => return vec![0u8; size],
        };

    let cr = match Context::new(&surface) {
        Ok(c) => c,
        Err(_) => return vec![0u8; size],
    };

    // Dimmed background
    cr.set_operator(cairo::Operator::Source);
    cr.set_source_rgba(0.0, 0.0, 0.0, BG_ALPHA);
    cr.paint().ok();
    cr.set_operator(cairo::Operator::Over);

    if workspaces.is_empty() {
        drop(cr);
        return surface.take_data().map(|d| d.to_vec()).unwrap_or_default();
    }

    let n = workspaces.len();
    let cols = {
        let mut c = (n as f64).sqrt().ceil() as usize;
        if c == 0 { c = 1; }
        c
    };
    let rows = (n + cols - 1) / cols;

    let avail_w = width as f64 - CARD_PAD * (cols + 1) as f64;
    let avail_h = height as f64 - CARD_PAD * (rows + 1) as f64;
    let card_w = (avail_w / cols as f64).min(480.0);
    let card_h = (avail_h / rows as f64).min(320.0);

    let grid_w = cols as f64 * card_w + (cols - 1) as f64 * CARD_PAD;
    let grid_h = rows as f64 * card_h + (rows - 1) as f64 * CARD_PAD;
    let ox = (width as f64 - grid_w) / 2.0;
    let oy = (height as f64 - grid_h) / 2.0;

    let font = FontDescription::from_string("Sans 11");
    let label_font = FontDescription::from_string("Sans Bold 13");

    for (i, ws) in workspaces.iter().enumerate() {
        let col = i % cols;
        let row = i / cols;
        let cx = ox + col as f64 * (card_w + CARD_PAD);
        let cy = oy + row as f64 * (card_h + CARD_PAD);

        // Selection highlight
        if i == selected_index {
            rounded_rect(
                &cr,
                cx - SELECT_BORDER,
                cy - SELECT_BORDER,
                card_w + 2.0 * SELECT_BORDER,
                card_h + 2.0 * SELECT_BORDER,
                CARD_RADIUS + 2.0,
            );
            cr.set_source_rgba(0.4, 0.6, 1.0, 0.9);
            cr.fill().ok();
        }

        // Card background
        rounded_rect(&cr, cx, cy, card_w, card_h, CARD_RADIUS);
        cr.set_source_rgba(0.12, 0.12, 0.15, 0.95);
        cr.fill().ok();

        // Workspace label
        {
            let layout = pangocairo::functions::create_layout(&cr);
            let mut label = ws.id.to_string();
            if !ws.name.is_empty() && ws.name != label {
                label.push(' ');
                label.push_str(&ws.name);
            }
            layout.set_text(&label);
            layout.set_font_description(Some(&label_font));
            let (tw, _th) = layout.pixel_size();
            cr.set_source_rgba(0.85, 0.85, 0.9, 1.0);
            cr.move_to(cx + (card_w - tw as f64) / 2.0, cy + 6.0);
            pangocairo::functions::show_layout(&cr, &layout);
        }

        let win_y = cy + LABEL_HEIGHT;
        let win_h = card_h - LABEL_HEIGHT - THUMB_PAD;
        let win_w = card_w - 2.0 * THUMB_PAD;
        let win_x = cx + THUMB_PAD;

        if ws.clients.is_empty() {
            let layout = pangocairo::functions::create_layout(&cr);
            layout.set_text("(empty)");
            layout.set_font_description(Some(&font));
            let (tw, th) = layout.pixel_size();
            cr.set_source_rgba(0.5, 0.5, 0.55, 0.8);
            cr.move_to(cx + (card_w - tw as f64) / 2.0, win_y + (win_h - th as f64) / 2.0);
            pangocairo::functions::show_layout(&cr, &layout);
            continue;
        }

        // Find workspace bounds for scaling
        let min_x = ws.clients.iter().map(|c| c.x).min().unwrap_or(0);
        let min_y = ws.clients.iter().map(|c| c.y).min().unwrap_or(0);
        let max_x = ws.clients.iter().map(|c| c.x + c.w).max().unwrap_or(1);
        let max_y = ws.clients.iter().map(|c| c.y + c.h).max().unwrap_or(1);

        let ws_w = (max_x - min_x).max(1) as f64;
        let ws_h = (max_y - min_y).max(1) as f64;
        let scale = (win_w / ws_w).min(win_h / ws_h) * 0.9;

        let scaled_w = ws_w * scale;
        let scaled_h = ws_h * scale;
        let off_x = win_x + (win_w - scaled_w) / 2.0;
        let off_y = win_y + (win_h - scaled_h) / 2.0;

        for client in &ws.clients {
            draw_client(&cr, &font, client, min_x, min_y, scale, off_x, off_y, thumbnails);
        }
    }

    drop(cr);
    surface.take_data().map(|d| d.to_vec()).unwrap_or_default()
}
