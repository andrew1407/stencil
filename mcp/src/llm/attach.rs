//! Attachments — reading a local image into a §7 vision attachment.

use super::{ImageAttachment, MAX_IMAGE_BYTES};

/// Try to read a local image file into a vision attachment (contract §7 media types).
/// Returns `(attachment, note)`:
/// - a URL / non-existent path / directory → `(None, None)` — it's a CLI-side source
///   (web URL or server project name), silently sent text-only;
/// - an existing file with an unsupported extension, over the 8 MiB cap, or unreadable →
///   `(None, Some(note))` — sent text-only with a human-readable note;
/// - otherwise `(Some(attachment), None)`.
pub fn attach_local_image(input: &str) -> (Option<ImageAttachment>, Option<String>) {
    let path = std::path::Path::new(input);
    if !path.is_file() {
        return (None, None);
    }

    let extension = path
        .extension()
        .map(|e| e.to_string_lossy().to_ascii_lowercase())
        .unwrap_or_default();
    let media_type = match extension.as_str() {
        "png" => "image/png",
        "jpg" | "jpeg" => "image/jpeg",
        "webp" => "image/webp",
        "gif" => "image/gif",
        other => {
            return (
                None,
                Some(format!(
                    "note: input '{input}' (.{other}) is not an attachable image type \
                     (png/jpg/webp/gif) — the LLM was sent text only"
                )),
            );
        }
    };

    match std::fs::metadata(path) {
        Ok(meta) if meta.len() > MAX_IMAGE_BYTES => {
            return (
                None,
                Some(format!(
                    "note: input '{input}' is {} bytes, over the {MAX_IMAGE_BYTES}-byte \
                     attachment cap — the LLM was sent text only",
                    meta.len()
                )),
            );
        }
        Ok(_) => {}
        Err(e) => {
            return (
                None,
                Some(format!(
                    "note: could not stat input '{input}' ({e}) — the LLM was sent text only"
                )),
            );
        }
    }

    match std::fs::read(path) {
        Ok(bytes) => {
            use base64::Engine;
            let data = base64::engine::general_purpose::STANDARD.encode(&bytes);
            (
                Some(ImageAttachment {
                    media_type: media_type.to_string(),
                    data,
                }),
                None,
            )
        }
        Err(e) => (
            None,
            Some(format!(
                "note: could not read input '{input}' ({e}) — the LLM was sent text only"
            )),
        ),
    }
}

/// Wrap contour-rendered PNG bytes as the §7 edge-map attachment. `None` when the render
/// alone is over the 8 MiB cap — the edge map is best-effort and is dropped silently.
pub fn edge_map_attachment(bytes: &[u8]) -> Option<ImageAttachment> {
    if bytes.len() as u64 > MAX_IMAGE_BYTES {
        return None;
    }
    use base64::Engine;
    Some(ImageAttachment {
        media_type: "image/png".to_string(),
        data: base64::engine::general_purpose::STANDARD.encode(bytes),
    })
}

