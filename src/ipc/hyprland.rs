use std::io::{Read, Write};
use std::net::Shutdown;
use std::os::unix::net::UnixStream;

use super::{ClientInfo, MonitorInfo, WorkspaceInfo};


fn socket_path() -> Option<String> {
    let xdg = std::env::var("XDG_RUNTIME_DIR").ok()?;
    let sig = std::env::var("HYPRLAND_INSTANCE_SIGNATURE").ok()?;
    Some(format!("{xdg}/hypr/{sig}/.socket.sock"))
}

fn hypr_request(cmd: &str) -> Option<String> {
    let path = socket_path()?;
    let mut stream = UnixStream::connect(&path).ok()?;
    let msg = format!("j/{cmd}");
    stream.write_all(msg.as_bytes()).ok()?;
    stream.shutdown(Shutdown::Write).ok();
    let mut resp = String::new();
    stream.read_to_string(&mut resp).ok()?;
    Some(resp)
}

// Minimal JSON helpers — avoids pulling in a JSON library for Hyprland's flat JSON.

fn json_string<'a>(json: &'a str, key: &str) -> &'a str {
    let needle = format!("\"{key}\":");
    let Some(pos) = json.find(&needle) else { return "" };
    let rest = json[pos + needle.len()..].trim_start_matches([' ', '\t']);
    if rest.starts_with('"') {
        let inner = &rest[1..];
        let end = inner.find('"').unwrap_or(0);
        &inner[..end]
    } else {
        ""
    }
}

fn json_int(json: &str, key: &str) -> i64 {
    let needle = format!("\"{key}\":");
    let Some(pos) = json.find(&needle) else { return 0 };
    let rest = json[pos + needle.len()..].trim_start_matches([' ', '\t']);
    let num: String = rest
        .chars()
        .take_while(|c| c.is_ascii_digit() || *c == '-')
        .collect();
    num.parse().unwrap_or(0)
}

fn json_float(json: &str, key: &str) -> f64 {
    let needle = format!("\"{key}\":");
    let Some(pos) = json.find(&needle) else { return 0.0 };
    let rest = json[pos + needle.len()..].trim_start_matches([' ', '\t']);
    let num: String = rest
        .chars()
        .take_while(|c| c.is_ascii_digit() || *c == '-' || *c == '.')
        .collect();
    num.parse().unwrap_or(0.0)
}

fn json_array_objects(json: &str) -> Vec<&str> {
    let mut objs = Vec::new();
    let bytes = json.as_bytes();
    let mut depth = 0i32;
    let mut start = 0;
    let mut in_string = false;

    for (i, &c) in bytes.iter().enumerate() {
        if c == b'"' && (i == 0 || bytes[i - 1] != b'\\') {
            in_string = !in_string;
        }
        if in_string {
            continue;
        }
        match c {
            b'{' => {
                if depth == 0 {
                    start = i;
                }
                depth += 1;
            }
            b'}' => {
                depth -= 1;
                if depth == 0 {
                    objs.push(&json[start..=i]);
                }
            }
            _ => {}
        }
    }
    objs
}

fn parse_address(json: &str) -> u64 {
    let s = json_string(json, "address");
    let hex = s.strip_prefix("0x").unwrap_or(s);
    u64::from_str_radix(hex, 16).unwrap_or(0)
}

fn parse_array_pair(json: &str, key: &str) -> (i32, i32) {
    let needle = format!("\"{key}\":");
    let Some(pos) = json.find(&needle) else { return (0, 0) };
    let rest = &json[pos + needle.len()..];
    let Some(bracket) = rest.find('[') else { return (0, 0) };
    let inner = &rest[bracket + 1..];
    let a: i32 = inner
        .chars()
        .take_while(|c| c.is_ascii_digit() || *c == '-')
        .collect::<String>()
        .parse()
        .unwrap_or(0);
    let Some(comma) = inner.find(',') else { return (a, 0) };
    let b: i32 = inner[comma + 1..]
        .trim_start()
        .chars()
        .take_while(|c| c.is_ascii_digit() || *c == '-')
        .collect::<String>()
        .parse()
        .unwrap_or(0);
    (a, b)
}

pub fn get_workspaces() -> Vec<WorkspaceInfo> {
    let ws_json = hypr_request("workspaces").unwrap_or_default();
    let cl_json = hypr_request("clients").unwrap_or_default();

    let ws_objs = json_array_objects(&ws_json);
    let cl_objs = json_array_objects(&cl_json);

    let mut workspaces: Vec<WorkspaceInfo> = ws_objs
        .iter()
        .filter_map(|wj| {
            let id = json_int(wj, "id") as i32;
            if id < 1 {
                return None; // skip special workspaces
            }
            Some(WorkspaceInfo {
                id,
                name: json_string(wj, "name").to_owned(),
                monitor_id: json_int(wj, "monitorID") as i32,
                clients: Vec::new(),
            })
        })
        .collect();

    workspaces.sort_by_key(|w| w.id);

    for cj in &cl_objs {
        let mut workspace_id = json_int(cj, "workspace") as i32;

        // Hyprland may use nested "workspace":{"id":1,"name":"1"}
        if workspace_id == 0 {
            if let Some(pos) = cj.find("\"workspace\":") {
                if let Some(brace) = cj[pos..].find('{') {
                    let inner_start = pos + brace;
                    if let Some(end) = cj[inner_start..].find('}') {
                        let inner = &cj[inner_start..inner_start + end + 1];
                        workspace_id = json_int(inner, "id") as i32;
                    }
                }
            }
        }

        let (x, y) = parse_array_pair(cj, "at");
        let (w, h) = parse_array_pair(cj, "size");

        let ci = ClientInfo {
            class_name: json_string(cj, "class").to_owned(),
            title: json_string(cj, "title").to_owned(),
            address: parse_address(cj),
            workspace_id,
            x,
            y,
            w,
            h,
        };

        if let Some(ws) = workspaces.iter_mut().find(|w| w.id == workspace_id) {
            ws.clients.push(ci);
        }
    }

    workspaces
}

fn parse_monitors(json: &str) -> Vec<MonitorInfo> {
    json_array_objects(json)
        .iter()
        .filter_map(|mj| {
            let scale = json_float(mj, "scale");
            if scale <= 0.0 {
                return None;
            }
            // `width`/`height` are the mode in pixels, before scale and rotation.
            let mut w = (json_int(mj, "width") as f64 / scale).ceil() as i32;
            let mut h = (json_int(mj, "height") as f64 / scale).ceil() as i32;
            if json_int(mj, "transform") % 2 == 1 {
                std::mem::swap(&mut w, &mut h);
            }
            Some(MonitorInfo {
                id: json_int(mj, "id") as i32,
                x: json_int(mj, "x") as i32,
                y: json_int(mj, "y") as i32,
                w,
                h,
            })
        })
        .collect()
}

pub fn get_monitors() -> Vec<MonitorInfo> {
    parse_monitors(&hypr_request("monitors").unwrap_or_default())
}

fn dispatch(cmd: &str) {
    let Some(path) = socket_path() else { return };
    let Ok(mut stream) = UnixStream::connect(&path) else { return };
    let msg = format!("/dispatch {cmd}");
    let _ = stream.write_all(msg.as_bytes());
    let _ = stream.shutdown(Shutdown::Write);
    let mut buf = [0u8; 256];
    let _ = stream.read(&mut buf);
}

pub fn switch_workspace(id: i32) {
    dispatch(&format!("hl.dsp.focus({{ workspace = \"{id}\" }})"));
}

pub fn move_window_to_workspace(window_address: u64, workspace_id: i32) {
    dispatch(&format!(
        "hl.dsp.window.move({{ workspace = {workspace_id}, window = \"address:0x{window_address:x}\", silent = true }})"
    ));
}

pub fn get_active_workspace_name() -> String {
    let json = hypr_request("activeworkspace").unwrap_or_default();
    json_string(&json, "name").to_owned()
}

/// Returns the address of the currently focused window, or 0 if none.
pub fn get_active_window_address() -> u64 {
    let json = hypr_request("activewindow").unwrap_or_default();
    // Hyprland returns `{}` when no window is focused
    if json.trim() == "{}" {
        return 0;
    }
    parse_address(&json)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn client(x: i32, y: i32, w: i32, h: i32) -> ClientInfo {
        ClientInfo {
            class_name: String::new(),
            title: String::new(),
            address: 0,
            workspace_id: 1,
            x,
            y,
            w,
            h,
        }
    }

    #[test]
    fn monitor_rect_is_scaled_and_rotated() {
        let json = r#"[{
            "id": 1, "name": "DP-1", "width": 3840, "height": 2160, "x": 1934, "y": 0,
            "activeWorkspace": { "id": 4, "name": "4" },
            "scale": 1.5, "transform": 3
        },{
            "id": 2, "name": "DP-2", "width": 3840, "height": 2160, "x": 3374, "y": 835,
            "activeWorkspace": { "id": 5, "name": "5" },
            "scale": 1.5, "transform": 0
        }]"#;
        let monitors = parse_monitors(json);
        assert_eq!(monitors.len(), 2);
        let (m1, m2) = (monitors[0], monitors[1]);
        assert_eq!((m1.id, m1.x, m1.y, m1.w, m1.h), (1, 1934, 0, 1440, 2560));
        assert_eq!((m2.id, m2.x, m2.y, m2.w, m2.h), (2, 3374, 835, 2560, 1440));
    }

    #[test]
    fn overlap_needs_at_least_one_pixel() {
        let m = MonitorInfo { id: 2, x: 3374, y: 835, w: 2560, h: 1440 };
        assert!(m.overlaps(&client(3474, 935, 600, 400)));
        assert!(m.overlaps(&client(4354, 436, 600, 400))); // one row on the monitor
        assert!(!m.overlaps(&client(4354, 435, 600, 400))); // touching the top edge
        assert!(!m.overlaps(&client(1994, 60, 600, 400))); // on a neighbouring monitor
        assert!(!m.overlaps(&client(-12000, 200, 600, 400)));
    }
}
