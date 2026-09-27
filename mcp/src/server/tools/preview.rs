//! The opt-in `preview`: each written image re-rendered as a small PNG, attached as an MCP image
//! block after the summary and the JSON payload, and named in the payload by its size. A preview
//! that cannot be rendered never fails the call; the summary says why instead.

use base64::Engine;
use rmcp::model::{CallToolResult, Content};
use schemars::JsonSchema;
use serde::Serialize;

use crate::pipeline;

/// Images one script call previews at most; a preview is a few hundred KiB of context.
pub const MAX_SCRIPT_PREVIEWS: usize = 4;

/// One attached preview: the written file it shows and the thumbnail's pixel size.
#[derive(Serialize, JsonSchema)]
pub struct PreviewNote {
    pub path: String,
    pub width: u32,
    pub height: u32,
}

/// The previews of one call: notes for the payload, image blocks for the result, and one
/// summary line per image, in the order the blocks are attached.
#[derive(Default)]
pub struct Previews {
    pub notes: Vec<PreviewNote>,
    pub lines: Vec<String>,
    blocks: Vec<Content>,
}

impl Previews {
    /// Render each of `paths`, written inside `root`, through the CLI's `--thumbnail`.
    pub async fn render(paths: &[&str], root: &str) -> Previews {
        let mut out = Previews::default();
        for path in paths {
            match pipeline::render_preview(path, root).await {
                Ok(p) => {
                    let data = base64::engine::general_purpose::STANDARD.encode(&p.png);
                    out.blocks.push(Content::image(data, "image/png"));
                    out.lines.push(format!("preview: {path} ({}x{}) attached", p.width, p.height));
                    let (path, width, height) = (path.to_string(), p.width, p.height);
                    out.notes.push(PreviewNote { path, width, height });
                }
                Err(e) => out.lines.push(format!("preview: could not render {path}: {e}")),
            }
        }
        out
    }

    /// No preview, only a summary line saying why.
    pub fn line(line: String) -> Previews {
        Previews { lines: vec![line], ..Previews::default() }
    }

    /// `result` with the image blocks after its text and JSON blocks.
    pub fn attach(self, mut result: CallToolResult) -> CallToolResult {
        result.content.extend(self.blocks);
        result
    }
}

#[cfg(test)]
mod tests {
    //! The blocks a preview adds, on the wire a client reads.

    use super::*;
    use crate::server::testwire::wire;

    #[test]
    fn previews_ride_after_the_summary_and_the_json_payload() {
        let blocks = vec![Content::text("wrote o.png (8x6)"), Content::text("{}")];
        let text = CallToolResult::success(blocks);
        let previews = Previews {
            notes: vec![PreviewNote { path: "o.png".into(), width: 8, height: 6 }],
            lines: vec!["preview: o.png (8x6) attached".into()],
            blocks: vec![Content::image("UE5HIQ==", "image/png")],
        };
        let wire = wire(&previews.attach(text));
        assert_eq!(wire["content"].as_array().unwrap().len(), 3);
        assert_eq!(wire["content"][2]["type"], "image");
        assert_eq!(wire["content"][2]["mimeType"], "image/png");
        assert_eq!(wire["content"][2]["data"], "UE5HIQ==");
    }

    #[test]
    fn a_line_alone_attaches_nothing() {
        let result = || CallToolResult::success(vec![Content::text("wrote p.stencil (project)")]);
        let previews = Previews::line("preview: none for a .stencil project".into());
        assert_eq!(wire(&previews.attach(result())), wire(&result()));
    }
}
