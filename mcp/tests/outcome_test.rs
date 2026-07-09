//! Parsing the CLI's stderr into structured results (pure).

use stencil_mcp::outcome::{extract_errors, parse_remotes, parse_scraped, parse_wrote, Remote};

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
fn parses_success_line_with_any_page_name_suffix() {
    // The page label names the format actually used (e.g. from `--blank b5`), not always A4.
    let w = parse_wrote("wrote /tmp/page.png (665x945 px · B5 17.6×25cm)\n").unwrap();
    assert_eq!(w.path, "/tmp/page.png");
    assert_eq!((w.width, w.height), (665, 945));
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

// ── source-site scrape (multi-file) ───────────────────────────────────────────

#[test]
fn parses_two_measured_images_and_summary() {
    let stderr = "wrote out/logo.png (200x80 px · source example.com)\n\
                  wrote out/hero.jpg (1280x720 px · source example.com)\n\
                  scraped 2 file(s) from example.com into out";
    let s = parse_scraped(stderr);
    assert_eq!(s.host.as_deref(), Some("example.com"));
    assert_eq!(s.dir.as_deref(), Some("out"));
    assert_eq!(s.files.len(), 2);
    assert_eq!(s.files[0].path, "out/logo.png");
    assert_eq!((s.files[0].width, s.files[0].height), (Some(200), Some(80)));
    assert_eq!(s.files[1].path, "out/hero.jpg");
    assert_eq!((s.files[1].width, s.files[1].height), (Some(1280), Some(720)));
}

#[test]
fn video_line_has_null_dims() {
    let s = parse_scraped("wrote assets/clip.mp4 (source cdn.test)\n");
    assert_eq!(s.files.len(), 1);
    assert_eq!(s.files[0].path, "assets/clip.mp4");
    assert_eq!((s.files[0].width, s.files[0].height), (None, None));
}

#[test]
fn path_containing_a_paren_uses_the_last_open_paren() {
    let s = parse_scraped("wrote out/img (1) (300x300 px · source example.org)\n");
    assert_eq!(s.files.len(), 1);
    assert_eq!(s.files[0].path, "out/img (1)");
    assert_eq!((s.files[0].width, s.files[0].height), (Some(300), Some(300)));
}

#[test]
fn wrote_line_without_parenthetical_is_all_path() {
    // "…or the rest of the line if no ' ('."
    let s = parse_scraped("wrote out/plain.png\n");
    assert_eq!(s.files.len(), 1);
    assert_eq!(s.files[0].path, "out/plain.png");
    assert_eq!((s.files[0].width, s.files[0].height), (None, None));
}

#[test]
fn per_item_error_lines_do_not_become_files() {
    let stderr = "wrote out/a.png (10x10 px · source x.test)\n\
                  error: could not fetch https://x.test/b.png (timeout)\n\
                  scraped 1 file(s) from x.test into out";
    let s = parse_scraped(stderr);
    assert_eq!(s.files.len(), 1);
    assert_eq!(s.files[0].path, "out/a.png");
}

#[test]
fn no_summary_leaves_dir_and_host_none() {
    let s = parse_scraped("wrote out/a.png (10x10 px · source x.test)\n");
    assert_eq!(s.dir, None);
    assert_eq!(s.host, None);
    assert_eq!(s.files.len(), 1);
}

/// The shared golden fixtures the CLI must reproduce and this parser must consume.
#[test]
fn parse_scraped_reproduces_the_golden_fixtures() {
    let raw = std::fs::read_to_string(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../cli/testdata/scrape_fixtures.json"
    ))
    .expect("read shared scrape fixtures");
    let doc: serde_json::Value = serde_json::from_str(&raw).expect("valid fixtures json");

    for case in doc["cases"].as_array().expect("cases[]") {
        let name = case["name"].as_str().unwrap();
        let stderr = case["stderr"].as_str().unwrap();
        let s = parse_scraped(stderr);

        // dir / host (both null in the error case).
        assert_eq!(s.dir.as_deref(), case["dir"].as_str(), "dir in {name}");
        assert_eq!(s.host.as_deref(), case["host"].as_str(), "host in {name}");

        // files[] — path + (possibly null) dims.
        let want = case["files"].as_array().unwrap();
        assert_eq!(s.files.len(), want.len(), "file count in {name}");
        for (got, want) in s.files.iter().zip(want) {
            assert_eq!(got.path, want["path"].as_str().unwrap(), "path in {name}");
            assert_eq!(
                got.width.map(u64::from),
                want["width"].as_u64(),
                "width in {name}"
            );
            assert_eq!(
                got.height.map(u64::from),
                want["height"].as_u64(),
                "height in {name}"
            );
        }

        // error — reuse extract_errors, as the pipeline does for the failure path. The
        // fixture stores the message without the `error: ` prefix the CLI prints.
        if let Some(err) = case["error"].as_str() {
            assert_eq!(extract_errors(stderr), format!("error: {err}"), "error in {name}");
        }
    }
}
