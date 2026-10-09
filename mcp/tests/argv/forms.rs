//! The forms a model writes that the CLI would misread: rotate in degrees, numeric crop
//! edges, a layout frame, the operator's token, and `--no-clobber`'s place in the argv.
use crate::common::args::params;

use serde_json::json;
use stencil_mcp::args::{build_argv, with_no_clobber};

fn refusal(value: serde_json::Value) -> String {
    build_argv(&params(value), None).expect_err("refused").to_string()
}

/// `rotate: 90` would be ninety quarter-turns; degrees are refused with the quarter-turn
/// count they meant, and anything else past three turns with the range.
#[test]
fn rotate_in_degrees_is_refused_with_the_quarter_turn_hint() {
    let degrees = refusal(json!({ "input": "a.png", "rotate": 90, "output": "o.png" }));
    assert!(degrees.contains("quarter-turns, not degrees: 90° = 1, so pass 1"), "{degrees}");
    let back = refusal(json!({ "input": "a.png", "rotate": -270, "output": "o.png" }));
    assert!(back.contains("pass -3 for -270°"), "{back}");
    let range = refusal(json!({ "input": "a.png", "rotate": 5, "output": "o.png" }));
    assert!(range.contains("out of range — pass -3..3"), "{range}");

    let ok = params(json!({ "input": "a.png", "rotate": -3, "output": "o.png" }));
    assert_eq!(build_argv(&ok, None).unwrap(), ["-i", "a.png", "-r", "-3", "o.png"]);
}

/// A crop edge may be a bare number of pixels as well as a token.
#[test]
fn numeric_crop_edges_ride_as_pixel_tokens() {
    let crop = json!({ "x1": 10, "x2": -12.5, "y2": "50%" });
    let p = params(json!({ "input": "a.png", "crop": crop, "output": "o.png" }));
    let argv = build_argv(&p, None).unwrap();
    assert_eq!(argv[2..4], ["-c", "x1=10 x2=-12.5 y2=50%"]);
}

#[test]
fn layout_frame_rides_with_a_layout_and_names_a_real_frame() {
    let p = params(json!({ "input": "a.png", "layout_frame": "source", "output": "o.png" }));
    let argv = build_argv(&p, Some("/tmp/l.json")).unwrap();
    assert_eq!(argv[2..6], ["-l", "/tmp/l.json", "--layout-frame", "source"]);

    let wrong = refusal(json!({ "input": "a.png", "layout_frame": "page", "output": "o.png" }));
    assert!(wrong.contains("use \"current\" or \"source\""), "{wrong}");
}

/// The token is never a parameter, and never argv: the server sets it for the child's
/// environment, where a `ps` listing cannot show it.
#[test]
fn the_operator_token_is_never_a_parameter_nor_argv() {
    let publish = json!({ "input": "a.png", "remote": "http://h:8090", "output": "o.png",
                          "token": "model-chosen", "server_tokens": "model-chosen" });
    let mut remote = params(publish);
    assert_eq!(remote.server_tokens, None, "a caller cannot set the tokens");
    remote.server_tokens = Some("http://h:8090=op-tok".into());
    let argv = build_argv(&remote, None).unwrap();
    assert!(!argv.iter().any(|t| t == "--token" || t.contains("op-tok")), "{argv:?}");
}

/// `--no-clobber` rides before the positional output, which stays the last token.
#[test]
fn no_clobber_keeps_the_output_last() {
    let argv = build_argv(&params(json!({ "input": "a.png", "output": "o" })), None).unwrap();
    assert_eq!(with_no_clobber(argv), ["-i", "a.png", "--no-clobber", "o"]);
}
