//! The parsers the script modes add to the stderr contract: every `wrote` line (one per
//! `@save`, so a whole-directory script is reportable), the CLI's `note:` lines, the
//! word-tailed document lines, and `--script-check`'s diagnostics.

use stencil_mcp::outcome::{
    parse_all_wrote, parse_diagnostics, parse_documents, parse_notes, parse_wrote,
    parse_wrote_project,
};

#[test]
fn every_save_line_is_parsed_in_order() {
    let stderr = "wrote shots/a-stencil.png (800x600)\n\
                  wrote shots/b-stencil.png (40x20 px · A4)\n";
    let all = parse_all_wrote(stderr);

    assert_eq!(all.len(), 2);
    assert_eq!(all[0].path, "shots/a-stencil.png");
    assert_eq!((all[0].width, all[0].height), (800, 600));
    assert_eq!(all[1].path, "shots/b-stencil.png");
    assert_eq!((all[1].width, all[1].height), (40, 20));
}

/// The one-shot parser is the first of the same list — the two can never disagree.
#[test]
fn the_single_line_parser_is_the_first_of_the_list() {
    let stderr = "wrote a.png (1x1)\nwrote b.png (2x2)\n";
    assert_eq!(parse_wrote(stderr), parse_all_wrote(stderr).into_iter().next());
    assert!(parse_all_wrote("nothing to report\n").is_empty());
}

/// A `.stencil` bundle line carries no dimensions, so it is not a written image.
#[test]
fn a_project_line_is_not_counted_as_a_file() {
    assert!(parse_all_wrote("wrote album.stencil (project)\n").is_empty());
}

#[test]
fn note_lines_come_back_without_their_prefix() {
    let stderr = "note: shots/: no files matched\n\
                  wrote a-stencil.png (2x2)\n\
                  note: the script saved nothing — add a @save\n";

    assert_eq!(
        parse_notes(stderr),
        ["shots/: no files matched", "the script saved nothing — add a @save"]
    );
    assert!(parse_notes("error: nope\nwrote a.png (1x1)\n").is_empty());
}

/// A file written to a path that itself contains " (" still parses — the same `rfind` rule
/// the one-shot parser uses.
#[test]
fn a_parenthesised_path_survives() {
    let all = parse_all_wrote("wrote shots/img (1)-stencil.png (12x8)\n");
    assert_eq!(all[0].path, "shots/img (1)-stencil.png");
    assert_eq!((all[0].width, all[0].height), (12, 8));
}

/// The payload's per-file object comes from `Wrote`'s own `Serialize`, not by hand.
#[test]
fn a_written_file_serializes_as_the_payload_object() {
    let all = parse_all_wrote("wrote out.png (3x4)\n");
    assert_eq!(
        serde_json::to_value(&all[0]).unwrap(),
        serde_json::json!({ "path": "out.png", "width": 3, "height": 4 })
    );
}

/// Word-tailed `wrote` lines are documents: a `.stencil` project and the emitted scripts.
#[test]
fn project_and_emit_lines_are_documents_not_images() {
    let stderr = "wrote a.png (4x4)\nwrote my (1) shot.stencil (project)\nwrote s.pystc (python)\n";
    assert_eq!(
        parse_documents(stderr),
        [
            ("my (1) shot.stencil".to_string(), "project".to_string()),
            ("s.pystc".to_string(), "python".to_string()),
        ]
    );
    assert_eq!(parse_wrote_project(stderr).as_deref(), Some("my (1) shot.stencil"));
    assert_eq!(parse_all_wrote(stderr).len(), 1, "only the image has a size");
}

/// `--script-check`'s stdout, one diagnostic per line, split from the right so a path with
/// a colon survives.
#[test]
fn check_lines_become_structured_diagnostics() {
    let stdout = "C:\\work\\a.stc:2:3: error: unknown directive '@crp' — did you mean \
                  '@crop'? [E_UNKNOWN_DIRECTIVE]\n\
                  a.stc:9:1: warning: tokens after a block's `:` [W_TRAILING_TOKENS]\n\
                  a banner line\n";
    let found = parse_diagnostics(stdout);
    assert_eq!(found.len(), 2);
    assert_eq!((found[0].line, found[0].col, found[0].severity.as_str()), (2, 3, "error"));
    assert_eq!(found[0].code, "E_UNKNOWN_DIRECTIVE");
    assert_eq!(found[0].message, "unknown directive '@crp' — did you mean '@crop'?");
    let second = (found[1].severity.as_str(), found[1].code.as_str());
    assert_eq!(second, ("warning", "W_TRAILING_TOKENS"));
}
