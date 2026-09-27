//! The §13 op registry as this server reads it: prompt pins (names vs the contract's mcp
//! surface, flags, key phrases — never block bytes), the registry/mapper cross-check, the
//! limits and the forbidden-op boundary. Generation FROM the registry lives in
//! `prompt_assembly_test.rs`; validating against it is core's (`opplan_fixtures_test.rs`).

use stencil_mcp::opplan::{from_result, OpPlanError};
use stencil_mcp::registry::{descriptor, forbidden_ops, is_forbidden, limit, op_registry};

/// The contract's mcp surface: core §2 + §2.1 in §2 order, minus `undo`/`redo`/`reset` — a
/// one-shot headless tool has no edit history to step.
const MCP_SURFACE: [&str; 10] = [
    "crop", "rotate", "filter", "layout", "formula", "page", "blank", "frame", "image", "save",
];

// ── §13(a): registered op NAMES == the contract's mcp surface ──

#[test]
fn the_registry_resolves_to_the_contracts_mcp_surface_in_prompt_order() {
    let names: Vec<&str> = op_registry().iter().map(|d| d.name).collect();
    assert_eq!(names, MCP_SURFACE);
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
        assert!(!d.bullet.is_empty(), "\"{}\" has no bullet", d.name);
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

// ── Registry names == mapper-known ops (cross-check of opplan's match arms) ──

#[test]
fn every_registered_op_is_known_to_the_typed_mapper() {
    // A registered op with no normalizer arm would fail every checked plan naming it.
    for d in op_registry() {
        let doc = format!(r#"{{"status":"valid","reply":"x","actions":[{{"op":"{}"}}]}}"#, d.name);
        if let Err(err) = from_result(&doc) {
            assert!(!err.to_string().contains("no normalizer"), "{}: {err}", d.name);
            assert!(matches!(err, OpPlanError::Action { .. }), "{}: {err}", d.name);
        }
    }
    let err = from_result(r#"{"status":"valid","reply":"x","actions":[{"op":"teleport"}]}"#).unwrap_err();
    assert!(err.to_string().contains("no normalizer"), "{err}");
}

#[test]
fn ops_outside_the_registry_have_no_descriptor() {
    // §10/§8 ops this surface deliberately does not register (no editor, no clipboard,
    // no theme store, no edit history, no extension profile) stay §1 unknown-op skips.
    for op in [
        "theme", "accent", "lineStyle", "units", "clear", "view", "connect", "disconnect",
        "openUrl", "copy", "removeProject", "clearProjects", "compare", "zoom", "undo",
        "redo", "reset", "focus", "open", "attach",
    ] {
        assert!(descriptor(op).is_none(), "\"{op}\" must not be registered");
    }
}

/// The one limit this server words itself is read off the registry, never mirrored.
#[test]
fn the_limits_come_from_the_registry() {
    assert_eq!(limit("ask.maxOptions"), Some(5.0));
    assert_eq!(limit("MAX_STRING_CHARS"), Some(5000.0));
    assert_eq!(limit("NO_SUCH_LIMIT"), None);
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
    assert!(is_forbidden("paste") && !is_forbidden("crop"));
}

#[test]
fn no_registry_entry_uses_a_forbidden_name() {
    for d in op_registry() {
        assert!(!is_forbidden(d.name), "registry entry \"{}\" is forbidden", d.name);
    }
}
