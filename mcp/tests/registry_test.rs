//! The §13 op registry and the schema engine it drives — two views of one table: prompt pins
//! (names vs the contract's mcp surface, flags, key phrases — never block bytes), the
//! registry/validator cross-check, limits, the forbidden-op boundary, the grammars.
//! Generation FROM the registry lives in `prompt_assembly_test.rs`.

use stencil_mcp::opplan::schema::{matches, schema};
use stencil_mcp::opplan::{parse_op_plan, OpPlanError, MAX_ASK_OPTIONS};
use stencil_mcp::registry::{descriptor, forbidden_ops, is_forbidden, op_registry};

/// The contract's mcp surface: core §2 + §2.1 in §2 order, minus `undo`/`redo`/`reset` — a
/// one-shot headless tool has no edit history to step.
const MCP_SURFACE: [&str; 10] = [
    "crop", "rotate", "filter", "layout", "formula", "page", "blank", "frame", "image", "save",
];

fn plan_with_op(op: &str) -> String {
    format!(r#"{{"version":1,"reply":"x","actions":[{{"op":"{op}","bogusfield":1}}]}}"#)
}

// ── §13(a): registered op NAMES == the contract's mcp surface ──

/// Both views resolve to the same surface in the same order: the prompt's descriptor table
/// and the registry entries the validator is driven by.
#[test]
fn the_registry_and_the_schema_resolve_to_the_contracts_mcp_surface_in_prompt_order() {
    let names: Vec<&str> = op_registry().iter().map(|d| d.name).collect();
    assert_eq!(names, MCP_SURFACE);

    let s = schema();
    assert_eq!(s.profile, "mcp");
    let entries: Vec<&str> = s.entries.iter().map(|e| e.name.as_str()).collect();
    assert_eq!(entries, MCP_SURFACE);
    assert_eq!(s.entry("crop").unwrap().rules, ["cropAspectFold"]);
}

// ── §13(b): per-op flags, on both sides ──

#[test]
fn only_image_and_save_are_top_level_only_and_only_frame_is_video_only() {
    for d in op_registry() {
        let top_level = matches!(d.name, "image" | "save");
        assert_eq!(d.top_level_only, top_level, "top_level_only flag of \"{}\"", d.name);
        assert_eq!(d.video_only, d.name == "frame", "video_only flag of \"{}\"", d.name);
        // Every registered op is capability-free on this surface — nothing optional is
        // wired here, so a capability-carrying entry would silently vanish from the prompt.
        assert_eq!(d.capability, None, "capability of \"{}\"", d.name);

        // The registry entry the validator resolves carries the same flag and a bullet.
        let e = schema().entry(d.name).expect(d.name);
        assert!(e.bullet.is_some(), "\"{}\" has no bullet", d.name);
        assert_eq!(e.flag("topLevelOnly"), top_level, "schema topLevelOnly of \"{}\"", d.name);
    }
}

// ── §13(c): one key semantic phrase per bullet (never block bytes) ──

#[test]
fn each_bullet_carries_its_key_semantic_phrase() {
    let phrases: [(&str, &str); 10] = [
        ("crop", "NEVER derive ratio tokens yourself"),
        ("rotate", "quarter turns only"),
        ("filter", "\"custom\" is a duotone tint and requires \"tint\""),
        ("layout", "draw annotation polylines in image-pixel coordinates"),
        ("formula", "single variable\n  matching the axis"),
        ("page", "ISO page formats a0–a10, b0–b10, c0–c10"),
        ("blank", "create a blank page"),
        ("frame", "only valid when the current input is a video"),
        ("image", "1-based, in attachment order"),
        ("save", "save the current image with its drawn lines as a\n  project"),
    ];
    for (name, phrase) in phrases {
        let d = descriptor(name).expect(name);
        assert!(d.bullet.contains(phrase), "\"{name}\" bullet lost its key phrase");
        assert!(
            d.bullet.starts_with(&format!("- {{\"op\":\"{name}\"")),
            "\"{name}\" bullet must open with its own op literal"
        );
    }
}

// ── Registry names == parser-known ops (cross-check of opplan's match arms) ──

#[test]
fn every_registered_op_is_known_to_the_validator() {
    // A known op with a bogus extra field FAILS the plan (contract §1) — proving the
    // validator has a real arm for it; an unknown op would only warn.
    for d in op_registry() {
        let err = parse_op_plan(&plan_with_op(d.name)).unwrap_err();
        match err {
            OpPlanError::Action { ref op, ref detail } => {
                assert_eq!(op, d.name);
                assert!(detail.contains("unknown field"), "{d:?}: {detail}");
            }
            other => panic!("\"{}\" probe: expected an action error, got {other}", d.name),
        }
    }
}

#[test]
fn ops_outside_the_registry_fall_to_the_unknown_op_skip() {
    // §10/§8 ops this surface deliberately does not register (no editor, no clipboard,
    // no theme store, no edit history, no extension profile) stay §1 unknown-op skips.
    for op in [
        "theme", "accent", "lineStyle", "units", "clear", "view", "connect", "disconnect",
        "openUrl", "copy", "removeProject", "clearProjects", "compare", "zoom", "undo",
        "redo", "reset", "focus", "open", "attach",
    ] {
        assert!(descriptor(op).is_none(), "\"{op}\" must not be registered");
        let plan = parse_op_plan(&plan_with_op(op)).unwrap();
        assert!(plan.actions.is_empty());
        assert_eq!(plan.warnings, vec![format!("Skipped unknown operation \"{op}\"")]);
    }
}

/// The §11 option cap is the one limit imported as a constant; the rest come off the registry.
#[test]
fn the_limits_come_from_the_registry() {
    let s = schema();
    assert_eq!(s.limit("ask.maxOptions") as usize, MAX_ASK_OPTIONS);
    assert_eq!(s.limit("MAX_ACTIONS"), 16.0);
    assert_eq!(s.limit("MAX_STRING_CHARS"), 5000.0);
}

// ── §13 forbidden ops ──

#[test]
fn forbidden_ops_cover_the_never_model_drivable_boundary() {
    // One representative name per §13 category.
    for op in ["llm", "apiKey", "paste", "hotkey", "quit", "chat", "shareTabs", "deleteRemote"] {
        assert!(is_forbidden(op), "\"{op}\" must be forbidden");
    }
    // And no forbidden name ever resolves to an active descriptor.
    for op in forbidden_ops() {
        assert!(descriptor(op).is_none(), "forbidden \"{op}\" resolved to a descriptor");
    }
    // The schema engine reads the same list.
    assert_eq!(forbidden_ops().to_vec(), schema().forbidden);
    assert!(schema().is_forbidden("paste") && !schema().is_forbidden("crop"));
}

#[test]
fn no_registry_entry_uses_a_forbidden_name() {
    for d in op_registry() {
        assert!(!is_forbidden(d.name), "registry entry \"{}\" is forbidden", d.name);
    }
}

#[test]
fn a_plan_naming_a_forbidden_op_is_rejected_not_skipped() {
    for probe in [
        r#"{"version":1,"reply":"x","actions":[{"op":"paste"}]}"#,
        r#"{"version":1,"reply":"x","actions":[],"variants":[{"label":"v","actions":[{"op":"llm"}]}]}"#,
    ] {
        let err = parse_op_plan(probe).unwrap_err();
        assert!(
            err.to_string().contains("never model-drivable"),
            "expected the §13 reject, got: {err}"
        );
    }
}

// ── The hand-written matchers vs the registry's regex sources ──

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

/// The envelope's variant objects take undeclared keys; an op never does.
#[test]
fn a_variant_object_tolerates_undeclared_keys_but_ops_stay_strict() {
    let plan = parse_op_plan(r#"{"reply":"x","variants":[{"label":"v","actions":[],"note":"x"}]}"#)
        .expect("allowUnknown on the envelope's variant objects");
    assert_eq!(plan.variants.len(), 1);
    let err = parse_op_plan(r#"{"reply":"x","actions":[{"op":"rotate","dir":"left","note":"x"}]}"#)
        .unwrap_err();
    assert!(err.to_string().contains("unknown field"), "got: {err}");
}
