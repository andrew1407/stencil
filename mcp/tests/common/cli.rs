//! Two [`CliRunner`]s that stand in for the binary, so `pipeline::run`'s whole flow runs
//! without a Zig toolchain: [`FakeCli`] records one argv and answers at once, [`SlowCli`]
//! takes its time so a fan-out's overlap and its cancellation can be watched.

use std::borrow::Cow;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

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

/// A runner that takes its time and answers with the output path it was handed. `finished`
/// only rises when a run reached the end of its call, which an aborted one never does, and
/// `peak` is the most runs that were ever in flight at once.
pub struct SlowCli {
    /// One delay per call, in arrival order; a call past the end waits none.
    delays: Vec<Duration>,
    started: AtomicUsize,
    finished: AtomicUsize,
    in_flight: AtomicUsize,
    peak: AtomicUsize,
}

impl SlowCli {
    pub fn with_delays(millis: &[u64]) -> Arc<SlowCli> {
        Arc::new(SlowCli {
            delays: millis.iter().map(|ms| Duration::from_millis(*ms)).collect(),
            started: AtomicUsize::new(0),
            finished: AtomicUsize::new(0),
            in_flight: AtomicUsize::new(0),
            peak: AtomicUsize::new(0),
        })
    }

    pub fn started(&self) -> usize {
        self.started.load(Ordering::SeqCst)
    }

    pub fn finished(&self) -> usize {
        self.finished.load(Ordering::SeqCst)
    }

    pub fn peak(&self) -> usize {
        self.peak.load(Ordering::SeqCst)
    }
}

impl CliRunner for SlowCli {
    async fn run(
        &self,
        argv: &[Cow<'static, str>],
        _dir: Option<&Path>,
    ) -> Result<CliOutput, String> {
        let index = self.started.fetch_add(1, Ordering::SeqCst);
        let flying = self.in_flight.fetch_add(1, Ordering::SeqCst) + 1;
        self.peak.fetch_max(flying, Ordering::SeqCst);
        tokio::time::sleep(self.delays.get(index).copied().unwrap_or_default()).await;
        self.in_flight.fetch_sub(1, Ordering::SeqCst);
        self.finished.fetch_add(1, Ordering::SeqCst);
        let output = argv.last().expect("an output path").to_string();
        Ok(CliOutput { success: true, stderr: format!("wrote {output} (1x1)\n") })
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
