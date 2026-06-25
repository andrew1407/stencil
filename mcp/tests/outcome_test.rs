//! Parsing the CLI's stderr into structured results (pure).

use stencil_mcp::outcome::{extract_errors, parse_wrote};

#[test]
fn parses_success_line() {
    let w = parse_wrote("wrote /tmp/out.png (800x600)\n").unwrap();
    assert_eq!(w.path, "/tmp/out.png");
    assert_eq!((w.width, w.height), (800, 600));
}

#[test]
fn parses_success_line_among_banner_noise() {
    let stderr = "  ___ stencil banner ___\nsome usage text\nwrote out.png (16x12)\n";
    let w = parse_wrote(stderr).unwrap();
    assert_eq!(w.path, "out.png");
    assert_eq!((w.width, w.height), (16, 12));
}

#[test]
fn handles_path_containing_a_paren() {
    let w = parse_wrote("wrote /tmp/my (final) shot.png (1920x1080)").unwrap();
    assert_eq!(w.path, "/tmp/my (final) shot.png");
    assert_eq!((w.width, w.height), (1920, 1080));
}

#[test]
fn no_success_line_returns_none() {
    assert!(parse_wrote("error: something went wrong").is_none());
}

#[test]
fn extracts_error_lines() {
    let stderr = "banner\nusage: stencil ...\nerror: could not parse --crop \"oops\"\n";
    assert_eq!(
        extract_errors(stderr),
        "error: could not parse --crop \"oops\""
    );
}

#[test]
fn falls_back_to_full_stderr_without_error_prefix() {
    assert_eq!(extract_errors("  unexpected text  "), "unexpected text");
}

#[test]
fn empty_stderr_has_a_message() {
    assert!(!extract_errors("").is_empty());
}
