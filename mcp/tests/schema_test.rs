//! The registry-driven schema engine (`opplan::schema`): the embedded registry resolves
//! to the contract's mcp surface, the hand-written grammar matchers agree with the
//! registry's regex sources on representative inputs, and the one local constant the
//! tests import is pinned to the registry.

use stencil_mcp::opplan::schema::{matches, schema};
use stencil_mcp::opplan::MAX_ASK_OPTIONS;
use stencil_mcp::registry::forbidden_ops;

#[test]
fn the_mcp_profile_resolves_to_the_contracts_surface_in_prompt_order() {
    let s = schema();
    assert_eq!(s.profile, "mcp");
    let names: Vec<&str> = s.entries.iter().map(|e| e.name.as_str()).collect();
    assert_eq!(
        names,
        ["crop", "rotate", "filter", "layout", "formula", "page", "blank", "frame", "image", "save"]
    );
    for e in &s.entries {
        assert!(e.bullet.is_some(), "\"{}\" has no bullet", e.name);
        assert_eq!(e.flag("topLevelOnly"), matches!(e.name.as_str(), "image" | "save"));
    }
    assert_eq!(s.entry("crop").unwrap().rules, ["cropAspectFold"]);
}

#[test]
fn limits_and_the_forbidden_list_come_from_the_registry() {
    let s = schema();
    assert_eq!(s.limit("ask.maxOptions") as usize, MAX_ASK_OPTIONS);
    assert_eq!(s.limit("MAX_ACTIONS"), 16.0);
    assert_eq!(s.limit("MAX_STRING_CHARS"), 5000.0);
    assert_eq!(forbidden_ops().to_vec(), s.forbidden);
    assert!(s.is_forbidden("paste") && !s.is_forbidden("crop"));
}

#[test]
fn the_hand_written_matchers_follow_the_registry_grammars() {
    let cases: [(&str, &[&str], &[&str]); 9] = [
        ("CROP_TOKEN", &["10", "-10%", "1.5px", ".5cm", "3in", "0"], &["", "+5", "5.", "1 0", "10pt", "1e3", "--1"]),
        ("CROP_ASPECT", &["3:4", "01:1", "16:9"], &["0:1", "1:0", "1.5:1", "3/4", "3:", ":4", "a:b"]),
        ("PAGE_FORMAT", &["a0", "a4", "a10", "b10", "c0"], &["A4", "a11", "d4", "a", "a04", "a4 "]),
        ("HEX", &["#000000", "#FFffFF", "#1a2B3c"], &["#fff", "000000", "#12345g", "#1234567", " #000000"]),
        ("CSS_NAME", &["red", "AliceBlue"], &["", "light blue", "red1", "#fff"]),
        ("FORMULA_X", &["x*2+10", "(x - 1) / 2", "x ** 2"], &["", "y*2", "x^2", "x*2;"]),
        ("FORMULA_Y", &["y*2+10", "2"], &["x*2", "y^2"]),
        ("HTTP_URL", &["https://e/x.png", "HTTP://e", "http://e/a?b=c#d"], &["ftp://e", "https://", "https://e x", "e/x.png", "https://e\n", " https://e"]),
        ("URL_SCHEME", &["https://e", "file:///tmp/p", "a+b.c-d://x", "s3://bucket/key"], &["keep/Here", "/abs/path", "1x://e", "://e", "a b://e", "~/Downloads"]),
    ];
    for (name, ok, bad) in cases {
        for s in ok {
            assert!(matches(name, s), "{name} should accept {s:?}");
        }
        for s in bad {
            assert!(!matches(name, s), "{name} should reject {s:?}");
        }
    }
}

#[test]
fn a_variant_object_tolerates_undeclared_keys_but_ops_stay_strict() {
    use stencil_mcp::opplan::parse_op_plan;
    let plan = parse_op_plan(r#"{"reply":"x","variants":[{"label":"v","actions":[],"note":"x"}]}"#)
        .expect("allowUnknown on the envelope's variant objects");
    assert_eq!(plan.variants.len(), 1);
    let err = parse_op_plan(r#"{"reply":"x","actions":[{"op":"rotate","dir":"left","note":"x"}]}"#)
        .unwrap_err();
    assert!(err.to_string().contains("unknown field"), "got: {err}");
}
