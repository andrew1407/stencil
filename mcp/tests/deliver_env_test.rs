//! The desktop app and the OS opener run on the GUI allowlist: neither ever sees the LLM key
//! or the server tokens. `/bin/sh` stubs stand in for both, so this binary sets `PATH`.
#![cfg(unix)]

use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use stencil_mcp::config::{Config, Surface};
use stencil_mcp::deliver::deliver;
use stencil_mcp::pipeline::EditResult;

/// A launcher stub that dumps its environment to `<name>.env` beside itself.
fn stub(dir: &Path, name: &str) -> PathBuf {
    use std::os::unix::fs::PermissionsExt;
    let path = dir.join(name);
    let dump = dir.join(format!("{name}.env"));
    std::fs::write(&path, format!("#!/bin/sh\nenv > \"{}\"\n", dump.display())).unwrap();
    std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o755)).unwrap();
    path
}

fn read_when_written(path: &Path) -> String {
    let deadline = Instant::now() + Duration::from_secs(20);
    while Instant::now() < deadline {
        match std::fs::read_to_string(path) {
            Ok(text) if text.contains("PATH=") => return text,
            _ => std::thread::sleep(Duration::from_millis(20)),
        }
    }
    panic!("{} was never written", path.display());
}

#[tokio::test]
async fn a_gui_launch_never_sees_the_secrets() {
    let dir = tempfile::tempdir().unwrap();
    let opener = if cfg!(target_os = "macos") { "open" } else { "xdg-open" };
    stub(dir.path(), opener);
    let desktop = stub(dir.path(), "stencil-desktop");
    std::env::set_var("PATH", format!("{}:/usr/bin:/bin", dir.path().display()));
    std::env::set_var("STENCIL_LLM_API_KEY", "sk-must-not-leak");
    std::env::set_var("STENCIL_MCP_SERVER_TOKENS", "http://h=must-not-leak");
    std::env::set_var("STENCIL_SERVER_TOKENS", "http://h=must-not-leak");

    let image = dir.path().join("out.png");
    std::fs::write(&image, b"\x89PNG\r\n").unwrap();
    let path = image.to_string_lossy().into_owned();
    let result = EditResult { path, width: Some(1), height: Some(1), remotes: Vec::new() };
    let config = Config { desktop_path: Some(desktop), auto_open: true, ..Config::default() };
    let notes = deliver(&[Surface::Desktop, Surface::Browser], &result, &config).await;
    assert!(notes.iter().all(|n| n.ok), "{notes:?}");

    for name in ["stencil-desktop", opener] {
        let env = read_when_written(&dir.path().join(format!("{name}.env")));
        assert!(!env.contains("must-not-leak"), "a secret reached {name}:\n{env}");
    }
}
