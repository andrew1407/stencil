//! The tool summary an edit reports back (pure).
//!
//! `EditResult::summary` is the head of every `stencil_edit` reply. Its first line is the
//! CLI's own `wrote {path} ({w}x{h})` contract — the string calling agents and this repo's
//! other CLI wrappers key off — followed by one line per collaboration-server delivery.

use stencil_mcp::outcome::Remote;
use stencil_mcp::pipeline::EditResult;

fn result(remotes: Vec<Remote>) -> EditResult {
    EditResult {
        path: "/tmp/out.png".into(),
        width: 800,
        height: 600,
        remotes,
    }
}

/// A purely local edit is exactly the CLI's success line — no trailing newline, nothing else.
#[test]
fn a_local_edit_summarizes_as_the_cli_wrote_line() {
    assert_eq!(result(vec![]).summary(), "wrote /tmp/out.png (800x600)");
}

#[test]
fn an_updated_project_adds_one_delivery_line() {
    let summary = result(vec![Remote::Updated {
        id: "p_abc".into(),
        width: 800,
        height: 600,
    }])
    .summary();

    assert_eq!(
        summary,
        "wrote /tmp/out.png (800x600)\n↑ server: updated project p_abc (800x600)"
    );
}

#[test]
fn a_created_project_adds_one_delivery_line() {
    let summary = result(vec![Remote::Created {
        name: "My Shot".into(),
        id: "p_new".into(),
    }])
    .summary();

    assert_eq!(
        summary,
        "wrote /tmp/out.png (800x600)\n↑ server: created project \"My Shot\" (p_new)"
    );
}

/// One call can both update a fetched project and create a new one; both are reported, in
/// the order the CLI printed them.
#[test]
fn several_deliveries_are_listed_in_order_one_per_line() {
    let summary = result(vec![
        Remote::Updated {
            id: "p_one".into(),
            width: 10,
            height: 20,
        },
        Remote::Created {
            name: "Second".into(),
            id: "p_two".into(),
        },
    ])
    .summary();

    let lines: Vec<&str> = summary.lines().collect();
    assert_eq!(lines.len(), 3, "one write line plus one line per delivery");
    assert_eq!(lines[0], "wrote /tmp/out.png (800x600)");
    assert_eq!(lines[1], "↑ server: updated project p_one (10x20)");
    assert_eq!(lines[2], "↑ server: created project \"Second\" (p_two)");
}

/// The write line reports the FINAL image dimensions, which need not match a delivery's
/// (a server may hold a differently-sized render). The two are independent fields.
#[test]
fn the_write_line_reports_the_local_dimensions_independently() {
    let summary = EditResult {
        path: "out.webp".into(),
        width: 1920,
        height: 1080,
        remotes: vec![Remote::Updated {
            id: "p_x".into(),
            width: 640,
            height: 480,
        }],
    }
    .summary();

    assert!(summary.starts_with("wrote out.webp (1920x1080)\n"));
    assert!(summary.ends_with("updated project p_x (640x480)"));
}

/// Paths with spaces are passed through verbatim — the summary is prose, not a shell
/// command, so nothing quotes or escapes them.
#[test]
fn a_path_with_spaces_is_not_quoted_or_escaped() {
    let summary = EditResult {
        path: "/tmp/my shot (final).png".into(),
        width: 4,
        height: 4,
        remotes: vec![],
    }
    .summary();
    assert_eq!(summary, "wrote /tmp/my shot (final).png (4x4)");
}
