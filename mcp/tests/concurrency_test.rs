//! The process-wide cap on concurrent CLI children (`STENCIL_MCP_MAX_CONCURRENT_CLI`,
//! default 2), watched through a `/bin/sh` stub that logs when each run starts and ends.
#![cfg(unix)]

use std::path::Path;

use stencil_mcp::config::max_concurrent_cli;

fn stub(dir: &Path) -> std::path::PathBuf {
    use std::os::unix::fs::PermissionsExt;
    let path = dir.join("stencil-stub");
    let log = dir.join("runs.log");
    let script = format!(
        "#!/bin/sh\necho start >> \"{0}\"\nsleep 0.4\necho end >> \"{0}\"\n",
        log.display()
    );
    std::fs::write(&path, script).expect("write the stub");
    std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o755)).expect("chmod +x");
    path
}

#[tokio::test]
async fn no_more_than_the_cap_of_cli_children_run_at_once() {
    assert_eq!(max_concurrent_cli(), 2, "the default");
    let dir = tempfile::tempdir().unwrap();
    std::env::set_var("STENCIL_CLI", stub(dir.path()));

    // Each probe is one CLI run; the stub prints no document, so each fails, which is fine.
    let probes = (0..4).map(|_| async {
        let argv = stencil_mcp::args::build_probe_argv("clip.mp4").unwrap();
        stencil_mcp::pipeline::run_probe(&argv).await
    });
    let _ = futures_join(probes.collect()).await;

    let log = std::fs::read_to_string(dir.path().join("runs.log")).unwrap();
    let mut running: i32 = 0;
    for line in log.lines() {
        running += if line == "start" { 1 } else { -1 };
        assert!(running <= 2, "three runs overlapped:\n{log}");
    }
    assert_eq!(log.lines().filter(|l| *l == "start").count(), 4, "{log}");

    // Read per call, never zero; the slots above were sized once, at the first spawn.
    std::env::set_var("STENCIL_MCP_MAX_CONCURRENT_CLI", "5");
    assert_eq!(max_concurrent_cli(), 5);
    std::env::set_var("STENCIL_MCP_MAX_CONCURRENT_CLI", "0");
    assert_eq!(max_concurrent_cli(), 2);
    std::env::remove_var("STENCIL_MCP_MAX_CONCURRENT_CLI");
}

/// Join a batch of futures without a futures crate: each runs as its own task.
async fn futures_join<F>(futures: Vec<F>) -> Vec<F::Output>
where
    F: std::future::Future + Send + 'static,
    F::Output: Send + 'static,
{
    let tasks: Vec<_> = futures.into_iter().map(tokio::spawn).collect();
    let mut out = Vec::with_capacity(tasks.len());
    for task in tasks {
        out.push(task.await.expect("a probe task"));
    }
    out
}
