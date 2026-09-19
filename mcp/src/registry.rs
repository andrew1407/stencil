//! The §13 op registry — the single source of the ops this server can plan and execute.
//!
//! Every op the `stencil_prompt` pipeline runs has exactly ONE entry carrying its prompt
//! bullet and flags; §4's "Available ops" section is generated from them and the validator
//! consults the same entries, so the prompt can never promise an op this surface lacks.

use std::sync::OnceLock;

use crate::opplan::fold::{self, Lower};
use crate::opplan::schema::schema;

/// One op this surface executes: name + verbatim prompt bullet + flags (contract §13).
#[derive(Debug, Clone, Copy)]
pub struct OpDescriptor {
    /// The wire `"op"` discriminator.
    pub name: &'static str,
    /// The exact `- {"op":…}` block the assembled §4 ops section carries — leading
    /// `- `, internal newlines and two-space continuations included, no trailing newline.
    pub bullet: &'static str,
    /// §2.1: allowed in top-level `actions` only (rejected inside variants and
    /// ask-option previews).
    pub top_level_only: bool,
    /// Valid only when the working input is a video.
    pub video_only: bool,
    /// The runtime capability the op needs, or `None`. An entry whose capability is not in
    /// [`WIRED_CAPABILITIES`] is excluded from prompt generation and validation (§13).
    pub capability: Option<&'static str>,
    /// How a validated action of this op folds into a CLI run — carried HERE so validation and
    /// dispatch come off one table. An entry with no lowering is excluded like an unwired one.
    pub lower: Lower,
}

/// The lowering registered for an op, or `None` when this surface cannot run it.
fn lowering(name: &str) -> Option<Lower> {
    Some(match name {
        "crop" => fold::crop,
        "rotate" => fold::rotate,
        "filter" => fold::filter,
        "layout" => fold::layout,
        "formula" => fold::formula,
        "page" => fold::page,
        "blank" => fold::blank,
        "frame" => fold::frame,
        // §2.1 ops split the plan instead of riding a run.
        "image" | "save" => fold::split,
        _ => return None,
    })
}

/// The runtime capabilities wired on THIS surface. A headless tool has none of the optional
/// ones, so every registered op below is capability-free.
pub const WIRED_CAPABILITIES: &[&str] = &[];

fn leak(s: &str) -> &'static str {
    Box::leak(s.to_string().into_boxed_str())
}

/// Valid only when the working input is a video — not a registry flag (the frame entry
/// records it in prose), so it stays a local list.
const VIDEO_ONLY_OPS: &[&str] = &["frame"];

/// The ops `stencil_prompt` executes, in prompt order, with their §4 bullets — the
/// registry's mcp-profile entries, built once on first use.
pub fn op_registry() -> &'static [OpDescriptor] {
    static OPS: OnceLock<Vec<OpDescriptor>> = OnceLock::new();
    OPS.get_or_init(|| {
        schema()
            .entries
            .iter()
            .filter_map(|e| {
                Some(OpDescriptor {
                    name: leak(&e.name),
                    bullet: leak(e.bullet.as_deref().unwrap_or_default()),
                    top_level_only: e.flag("topLevelOnly"),
                    video_only: VIDEO_ONLY_OPS.contains(&e.name.as_str()),
                    capability: None,
                    lower: lowering(&e.name)?,
                })
            })
            .collect()
    })
}

/// §13's never-model-drivable boundary, as op names (`forbidden.perSurface.mcp`). Two
/// teeth: no entry may use one, and the validator hard-fails a plan naming one.
pub fn forbidden_ops() -> &'static [&'static str] {
    static OPS: OnceLock<Vec<&'static str>> = OnceLock::new();
    OPS.get_or_init(|| schema().forbidden.iter().map(|s| leak(s)).collect())
}

/// True when `name` sits on the §13 never-model-drivable boundary.
pub fn is_forbidden(name: &str) -> bool {
    schema().is_forbidden(name)
}

/// Lowercase substrings no prompt bullet may match (§13 prompt censor). Deliberately NOT
/// the bare word "token" — crop's bullet legitimately speaks of cropSpec tokens.
pub const CENSOR_PATTERNS: &[&str] = &[
    "api key",
    "api-key",
    "api_key",
    "apikey",
    "bearer",
    "authorization",
    "secret",
    "access token",
    "auth token",
    "server token",
    "endpoint",
    "base url",
    "base-url",
    "base_url",
    "baseurl",
];

/// The first censor pattern `bullet` matches (case-insensitively), or `None` when clean.
pub fn censor_violation(bullet: &str) -> Option<&'static str> {
    let lower = bullet.to_ascii_lowercase();
    CENSOR_PATTERNS.iter().copied().find(|p| lower.contains(p))
}

/// Is an entry needing `capability` active given the surface's wired list?
fn capability_wired(capability: Option<&str>, wired: &[&str]) -> bool {
    capability.is_none_or(|c| wired.contains(&c))
}

/// Look up an ACTIVE op: registered, capability wired, not forbidden. An op with no active
/// entry falls to §1's unknown-op skip, matching what the generated prompt promised.
pub fn descriptor(name: &str) -> Option<&'static OpDescriptor> {
    op_registry().iter().find(|d| {
        d.name == name && capability_wired(d.capability, WIRED_CAPABILITIES) && !is_forbidden(d.name)
    })
}

/// Assemble the §4 "Available ops" bullets: unwired capabilities are excluded, and a
/// forbidden name or censor-matching bullet errors instead of leaking into the prompt.
pub fn assemble_ops_section(ops: &[OpDescriptor], wired: &[&str]) -> Result<String, String> {
    let mut bullets = Vec::with_capacity(ops.len());
    for op in ops {
        if !capability_wired(op.capability, wired) {
            continue;
        }
        if is_forbidden(op.name) {
            return Err(format!(
                "op \"{}\" is never model-drivable (llm-contract.md §13) and may not be registered",
                op.name
            ));
        }
        if let Some(pattern) = censor_violation(op.bullet) {
            return Err(format!(
                "the \"{}\" bullet matches the sensitive pattern \"{pattern}\" — refusing to \
                 emit it into the prompt (llm-contract.md §13 censor)",
                op.name
            ));
        }
        bullets.push(op.bullet);
    }
    Ok(bullets.join("\n"))
}
