//! The §13 op registry: §13-style prompt pins (name set vs the contract's mcp surface,
//! flags, key phrases — never block bytes), the registry/validator cross-check, the
//! forbidden-op boundary, the capability-exclusion mechanism, and the prompt censor.

use stencil_mcp::llm::llm_system_prompt;
use stencil_mcp::opplan::{parse_op_plan, OpPlanError};
use stencil_mcp::registry::{
    assemble_ops_section, censor_violation, descriptor, forbidden_ops, is_forbidden,
    op_registry, OpDescriptor, WIRED_CAPABILITIES,
};

/// The contract's mcp surface: core §2 + §2.1 in §2 order, minus `undo`/`redo`/`reset` —
/// a one-shot headless tool has no edit history to step (they fall to §1's unknown-op
/// skip, like clipboard/theme ops).
const MCP_SURFACE: [&str; 10] = [
    "crop", "rotate", "filter", "layout", "formula", "page", "blank", "frame", "image", "save",
];

fn plan_with_op(op: &str) -> String {
    format!(r#"{{"version":1,"reply":"x","actions":[{{"op":"{op}","bogusfield":1}}]}}"#)
}

// ── §13(a): registered op NAMES == the contract's mcp surface ──

#[test]
fn the_registry_names_are_exactly_the_contracts_mcp_surface_in_prompt_order() {
    let names: Vec<&str> = op_registry().iter().map(|d| d.name).collect();
    assert_eq!(names, MCP_SURFACE);
}

// ── §13(b): per-op flags ──

#[test]
fn only_image_and_save_are_top_level_only_and_only_frame_is_video_only() {
    for d in op_registry() {
        assert_eq!(
            d.top_level_only,
            matches!(d.name, "image" | "save"),
            "top_level_only flag of \"{}\"",
            d.name
        );
        assert_eq!(d.video_only, d.name == "frame", "video_only flag of \"{}\"", d.name);
        // Every registered op is capability-free on this surface — nothing optional is
        // wired here, so a capability-carrying entry would silently vanish from the prompt.
        assert_eq!(d.capability, None, "capability of \"{}\"", d.name);
    }
    assert!(WIRED_CAPABILITIES.is_empty());
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

// ── Generation: the prompt's ops section comes from the registry ──

#[test]
fn the_assembled_prompt_carries_every_registered_bullet_in_registry_order() {
    // Calling llm_system_prompt() in a test (debug) build also fires the transition
    // golden assertion in llm.rs — the byte-stability proof of the refactor.
    let prompt = llm_system_prompt();
    let mut cursor = 0usize;
    for d in op_registry() {
        let at = prompt[cursor..]
            .find(d.bullet)
            .unwrap_or_else(|| panic!("bullet of \"{}\" missing or out of order", d.name));
        cursor += at + d.bullet.len();
    }
    let ops = assemble_ops_section(op_registry(), WIRED_CAPABILITIES).unwrap();
    assert!(prompt.contains(&ops), "the prompt embeds the assembled ops section verbatim");
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

#[test]
fn assembly_refuses_a_forbidden_registry_entry() {
    let rogue = [OpDescriptor {
        name: "paste",
        bullet: "- {\"op\":\"paste\"} — read the clipboard.",
        top_level_only: false,
        video_only: false,
        capability: None,
    }];
    let err = assemble_ops_section(&rogue, &[]).unwrap_err();
    assert!(err.contains("never model-drivable"), "{err}");
}

// ── §13 capability truth (mechanism tested with a stub registry — mcp wires none) ──

#[test]
fn an_entry_whose_capability_is_not_wired_is_excluded_from_generation() {
    let sample = [
        OpDescriptor {
            name: "sampleAlways",
            bullet: "- {\"op\":\"sampleAlways\"} — always available.",
            top_level_only: false,
            video_only: false,
            capability: None,
        },
        OpDescriptor {
            name: "sampleCopy",
            bullet: "- {\"op\":\"sampleCopy\"} — needs a clipboard.",
            top_level_only: false,
            video_only: false,
            capability: Some("clipboard"),
        },
    ];
    let without = assemble_ops_section(&sample, &[]).unwrap();
    assert!(without.contains("sampleAlways") && !without.contains("sampleCopy"));

    let with = assemble_ops_section(&sample, &["clipboard"]).unwrap();
    assert_eq!(
        with,
        "- {\"op\":\"sampleAlways\"} — always available.\n- {\"op\":\"sampleCopy\"} — needs a clipboard."
    );
}

// ── §13 prompt censor ──

#[test]
fn assembly_errors_on_bullets_matching_sensitive_patterns() {
    for (bullet, pattern) in [
        ("- {\"op\":\"x\"} — set the API key first.", "api key"),
        ("- {\"op\":\"x\"} — send Bearer credentials.", "bearer"),
        ("- {\"op\":\"x\"} — point the endpoint at a host.", "endpoint"),
        ("- {\"op\":\"x\"} — include the access token.", "access token"),
    ] {
        assert_eq!(censor_violation(bullet), Some(pattern));
        let sample = [OpDescriptor {
            name: "x",
            bullet,
            top_level_only: false,
            video_only: false,
            capability: None,
        }];
        let err = assemble_ops_section(&sample, &[]).unwrap_err();
        assert!(err.contains(pattern) && err.contains("censor"), "{err}");
    }
}

#[test]
fn the_real_bullets_pass_the_censor_including_crops_cropspec_tokens() {
    for d in op_registry() {
        assert_eq!(censor_violation(d.bullet), None, "\"{}\" bullet", d.name);
    }
    // The word "tokens" (cropSpec tokens) is legitimate prompt vocabulary — the censor
    // matches secret-shaped patterns, not the bare word.
    assert!(descriptor("crop").unwrap().bullet.contains("tokens"));
}
