//! Generating the §4 prompt's ops section FROM the registry: every bullet in order, an
//! unwired capability excluded, the §13 censor refusing credential prose. The registry's own
//! pins are in `registry_test.rs`.

use stencil_mcp::llm::llm_system_prompt;
use stencil_mcp::opplan::fold;
use stencil_mcp::registry::{
    assemble_ops_section, censor_violation, descriptor, op_registry, OpDescriptor,
    WIRED_CAPABILITIES,
};

/// A stub entry: only the name, the bullet and the capability matter to assembly, and a
/// stub never lowers.
fn stub(name: &'static str, bullet: &'static str, capability: Option<&'static str>) -> OpDescriptor {
    OpDescriptor {
        name,
        bullet,
        top_level_only: false,
        video_only: false,
        capability,
        lower: fold::split,
    }
}

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

#[test]
fn assembly_refuses_a_forbidden_registry_entry() {
    let rogue = [stub("paste", "- {\"op\":\"paste\"} — read the clipboard.", None)];
    let err = assemble_ops_section(&rogue, &[]).unwrap_err();
    assert!(err.contains("never model-drivable"), "{err}");
}

// ── §13 capability truth (mechanism tested with a stub registry — mcp wires none) ──

#[test]
fn an_entry_whose_capability_is_not_wired_is_excluded_from_generation() {
    assert!(WIRED_CAPABILITIES.is_empty());
    let sample = [
        stub("sampleAlways", "- {\"op\":\"sampleAlways\"} — always available.", None),
        stub(
            "sampleCopy",
            "- {\"op\":\"sampleCopy\"} — needs a clipboard.",
            Some("clipboard"),
        ),
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
        let sample = [stub("x", bullet, None)];
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
