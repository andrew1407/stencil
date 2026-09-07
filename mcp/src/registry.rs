//! The §13 op registry — the single source of the ops this server can plan and execute.
//!
//! Port of `llm-contract.md` §4 (two-part prompt rule) + §13: every op the `stencil_prompt`
//! pipeline executes has exactly ONE entry here carrying its prompt bullet and flags, the
//! §4 "Available ops" section is GENERATED from these entries (never hand-embedded), and
//! `opplan`'s validator consults the same entries for known-ness — so the prompt can never
//! promise an op this surface cannot run.
//!
//! The entries and the forbidden list are this surface's view of the shared
//! `browser/js/config/llm/opRegistry.json` (profile `mcp`, in prompt order; bullets are
//! the entry's bullet or its `bulletVariants.mcp`), built once from `opplan::schema`.
//! Ops that need a runtime capability this server does not wire (clipboard, theme store,
//! …) simply have no entries; [`WIRED_CAPABILITIES`] exists so a future entry CAN declare
//! a capability and be excluded automatically until it is wired.

use std::ops::Deref;
use std::sync::OnceLock;

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
    /// The runtime capability the op needs, or `None` for always-available ops. An entry
    /// whose capability is not in [`WIRED_CAPABILITIES`] is EXCLUDED from prompt
    /// generation and validation — the op falls to §1's unknown-op skip, and the model
    /// was never promised it (§13 capability truth).
    pub capability: Option<&'static str>,
}

/// The runtime capabilities wired on THIS surface. A headless MCP tool has none of the
/// optional ones (no clipboard, no theme store, no edit history), so the list is empty —
/// every registered op below is capability-free.
pub const WIRED_CAPABILITIES: &[&str] = &[];

/// A registry-backed table built on first use that reads like a `&'static [T]`.
pub struct Table<T: 'static>(fn() -> &'static [T]);

impl<T> Clone for Table<T> {
    fn clone(&self) -> Self {
        *self
    }
}
impl<T> Copy for Table<T> {}
impl<T> Deref for Table<T> {
    type Target = [T];
    fn deref(&self) -> &[T] {
        (self.0)()
    }
}
impl<T> AsRef<[T]> for Table<T> {
    fn as_ref(&self) -> &[T] {
        (self.0)()
    }
}
impl<T> IntoIterator for Table<T> {
    type Item = &'static T;
    type IntoIter = std::slice::Iter<'static, T>;
    fn into_iter(self) -> Self::IntoIter {
        (self.0)().iter()
    }
}

fn leak(s: &str) -> &'static str {
    Box::leak(s.to_string().into_boxed_str())
}

/// Valid only when the working input is a video — not a registry flag (the frame entry
/// records it in prose), so it stays a local list.
const VIDEO_ONLY_OPS: &[&str] = &["frame"];

/// The ops `stencil_prompt` executes, in prompt order, with their §4 bullets — the
/// registry's mcp-profile entries.
pub static OP_REGISTRY: Table<OpDescriptor> = Table(op_registry);

fn op_registry() -> &'static [OpDescriptor] {
    static OPS: OnceLock<Vec<OpDescriptor>> = OnceLock::new();
    OPS.get_or_init(|| {
        schema()
            .entries
            .iter()
            .map(|e| OpDescriptor {
                name: leak(&e.name),
                bullet: leak(e.bullet.as_deref().unwrap_or_default()),
                top_level_only: e.flag("topLevelOnly"),
                video_only: VIDEO_ONLY_OPS.contains(&e.name.as_str()),
                capability: None,
            })
            .collect()
    })
}

/// §13's never-model-drivable boundary, as op names — the registry's
/// `forbidden.perSurface.mcp`: llm/provider self-configuration, clipboard READS, hotkey
/// rebinding, session/window end, chat persistence/consent toggles, and server-side
/// destruction beyond what §10 grants. Two teeth: no registry entry may use one of these
/// names (tested + rejected at assembly), and the plan validator hard-fails any plan
/// naming one instead of skipping it as unknown.
pub static FORBIDDEN_OPS: Table<&'static str> = Table(forbidden_ops);

fn forbidden_ops() -> &'static [&'static str] {
    static OPS: OnceLock<Vec<&'static str>> = OnceLock::new();
    OPS.get_or_init(|| schema().forbidden.iter().map(|s| leak(s)).collect())
}

/// True when `name` sits on the §13 never-model-drivable boundary.
pub fn is_forbidden(name: &str) -> bool {
    schema().is_forbidden(name)
}

/// Lowercase substrings no prompt bullet may match (§13 prompt censor): api keys, bearer
/// tokens, endpoint-setting instructions. Deliberately NOT the bare word "token" — crop's
/// bullet legitimately speaks of cropSpec tokens.
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

/// Look up an ACTIVE op by name: registered, capability wired, not forbidden. This is the
/// validator's single known-ness gate — an op with no active entry falls to §1's
/// unknown-op skip, exactly matching what the generated prompt promised.
pub fn descriptor(name: &str) -> Option<&'static OpDescriptor> {
    OP_REGISTRY.iter().find(|d| {
        d.name == name && capability_wired(d.capability, WIRED_CAPABILITIES) && !is_forbidden(d.name)
    })
}

/// Assemble the §4 "Available ops" bullets from a registry: entries whose capability is
/// not wired are excluded (§13 capability truth); a forbidden name or a censor-matching
/// bullet is a registry mistake and errors instead of leaking into the prompt.
pub fn assemble_ops_section(ops: impl AsRef<[OpDescriptor]>, wired: &[&str]) -> Result<String, String> {
    let ops = ops.as_ref();
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
