//! The opt-in preview of a written image: the CLI's `--thumbnail` re-renders it as a PNG no
//! longer than [`PREVIEW_SIDE`] a side, into a temp file inside the run's root that is gone
//! once its bytes are read. The codec and the downscale are the CLI's; this names the files.

use std::borrow::Cow;

use super::progress::{self, Sink};
use super::CliRunner;
use crate::{confine, outcome};

/// The longest side of a preview, px.
pub const PREVIEW_SIDE: u32 = 512;

/// One rendered preview: the PNG bytes and the size the CLI reported for them.
pub struct Preview {
    pub png: Vec<u8>,
    pub width: u32,
    pub height: u32,
}

/// Render `image` (an absolute path inside `root`) to a preview, spawned inside `root` with
/// `--confine-output` like every write.
pub async fn render<R: CliRunner>(runner: &R, image: &str, root: &str) -> Result<Preview, String> {
    let temp = tempfile::Builder::new()
        .prefix(".stencil-preview-")
        .suffix(".png")
        .tempfile_in(root)
        .map_err(|e| format!("could not create a preview file in '{root}': {e}"))?;
    let argv = [
        Cow::Borrowed("-i"),
        Cow::Owned(image.to_string()),
        Cow::Borrowed("--thumbnail"),
        Cow::Owned(PREVIEW_SIDE.to_string()),
        Cow::Owned(temp.path().to_string_lossy().into_owned()),
    ];
    let Some(run) = confine::confine(root, &argv) else {
        return Err(format!("refusing a preview outside '{root}'"));
    };
    // Its `wrote` line names the temp file, which is no step of the call.
    let quiet = Some(Sink::new(|_| {}));
    let output = progress::scope(quiet, runner.run(&run.argv, Some(&run.dir))).await?;
    if !output.success {
        return Err(outcome::extract_errors(&output.stderr));
    }
    let wrote = outcome::parse_wrote(&output.stderr)
        .ok_or_else(|| "the stencil CLI printed no 'wrote' line for it".to_string())?;
    let path = temp.path().to_path_buf();
    let png = tokio::task::spawn_blocking(move || std::fs::read(path))
        .await
        .map_err(|e| format!("could not read it back: {e}"))?
        .map_err(|e| format!("could not read it back: {e}"))?;
    Ok(Preview { png, width: wrote.width, height: wrote.height })
}
