//! A [`CliRunner`] that records instead of spawning, so `pipeline::run`'s whole flow runs
//! without a Zig toolchain.

use std::borrow::Cow;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

use stencil_mcp::pipeline::{CliOutput, CliRunner};

/// One recorded invocation: the argv, and the working directory a confined run names.
pub struct Call {
    pub argv: Vec<String>,
    pub dir: Option<PathBuf>,
}

/// Records what it was handed and answers with a canned capture. `writes` makes it act like
/// a render: the bytes land at the argv's last token, the output path.
pub struct FakeCli {
    success: bool,
    stderr: String,
    writes: Option<Vec<u8>>,
    pub calls: Mutex<Vec<Call>>,
}

impl FakeCli {
    pub fn ok(stderr: &str) -> FakeCli {
        FakeCli { success: true, stderr: stderr.into(), writes: None, calls: Mutex::new(Vec::new()) }
    }

    pub fn failing(stderr: &str) -> FakeCli {
        FakeCli { success: false, stderr: stderr.into(), writes: None, calls: Mutex::new(Vec::new()) }
    }

    pub fn rendering(stderr: &str, bytes: &[u8]) -> FakeCli {
        FakeCli { writes: Some(bytes.to_vec()), ..FakeCli::ok(stderr) }
    }

    /// The one run that happened.
    pub fn call(&self) -> Call {
        let mut calls = self.calls.lock().unwrap();
        assert_eq!(calls.len(), 1, "expected exactly one CLI run");
        calls.pop().unwrap()
    }

    pub fn argv(&self) -> Vec<String> {
        self.call().argv
    }

    pub fn never_ran(&self) -> bool {
        self.calls.lock().unwrap().is_empty()
    }
}

impl CliRunner for FakeCli {
    async fn run(
        &self,
        argv: &[Cow<'static, str>],
        dir: Option<&Path>,
    ) -> Result<CliOutput, String> {
        let argv: Vec<String> = argv.iter().map(|a| a.to_string()).collect();
        if let Some(bytes) = &self.writes {
            let out = argv.last().expect("an output path");
            std::fs::write(out, bytes).expect("the fake render writes its output");
        }
        self.calls.lock().unwrap().push(Call { argv, dir: dir.map(Path::to_path_buf) });
        Ok(CliOutput { success: self.success, stderr: self.stderr.clone() })
    }
}
