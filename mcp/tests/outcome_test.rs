//! Parsing the CLI's stderr into structured results (pure).

use stencil_mcp::outcome::{extract_errors, parse_remotes, parse_wrote, Remote};

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
fn parses_success_line_with_page_metadata() {
    let w = parse_wrote("wrote /tmp/rotated.png (12x16 px · A4 21×29.7cm)\n").unwrap();
    assert_eq!(w.path, "/tmp/rotated.png");
    assert_eq!((w.width, w.height), (12, 16));
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

#[test]
fn parses_remote_update_line() {
    let r = parse_remotes("wrote out.png (800x600)\nupdated server result for project p_x_y (800x600)\n");
    assert_eq!(
        r,
        vec![Remote::Updated {
            id: "p_x_y".into(),
            width: 800,
            height: 600
        }]
    );
}

#[test]
fn parses_remote_create_line() {
    let r = parse_remotes("wrote out.png (16x12)\ncreated server project \"My Shot\" (p_a_b)\n");
    assert_eq!(
        r,
        vec![Remote::Created {
            name: "My Shot".into(),
            id: "p_a_b".into()
        }]
    );
}

#[test]
fn parses_both_server_deliveries_in_one_run() {
    // A call can update the fetched project and create a new one on another server.
    let stderr = "wrote out.png (10x10)\n\
                  updated server result for project p_1 (10x10)\n\
                  created server project \"Copy\" (p_2)\n";
    let r = parse_remotes(stderr);
    assert_eq!(
        r,
        vec![
            Remote::Updated {
                id: "p_1".into(),
                width: 10,
                height: 10
            },
            Remote::Created {
                name: "Copy".into(),
                id: "p_2".into()
            }
        ]
    );
}

#[test]
fn no_server_lines_yields_empty() {
    assert!(parse_remotes("wrote out.png (10x10)").is_empty());
}
