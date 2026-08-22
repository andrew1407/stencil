//! The §13 op registry — the single source of the ops this server can plan and execute.
//!
//! Port of `llm-contract.md` §4 (two-part prompt rule) + §13: every op the `stencil_prompt`
//! pipeline executes has exactly ONE entry here carrying its prompt bullet and flags, the
//! §4 "Available ops" section is GENERATED from these entries (never hand-embedded), and
//! `opplan`'s validator consults the same entries for known-ness — so the prompt can never
//! promise an op this surface cannot run.
//!
//! This surface registers the contract's mcp surface: core §2 + §2.1, in §2 order, minus
//! `undo`/`redo`/`reset` — a one-shot headless tool has no edit history to step. Ops that
//! need a runtime capability this server does not wire (clipboard, theme store, …) simply
//! have no entries; [`WIRED_CAPABILITIES`] exists so a future entry CAN declare a
//! capability and be excluded automatically until it is wired.

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

/// The ops `stencil_prompt` executes, in the §2 prompt order. Bullets are the §4 ops
/// section, one entry each.
pub const OP_REGISTRY: &[OpDescriptor] = &[
    OpDescriptor {
        name: "crop",
        bullet: r##"- {"op":"crop","spec":{"x1":"10%","x2":"-10%","aspect":"3:4"}} — move edges inward;
  tokens are numbers with optional unit % / px / cm / in; a leading "-" measures from the
  opposite side. Include only the edges you want to move. For a target aspect ratio add
  "aspect":"W:H" INSIDE "spec", never beside it (portrait "3:4", album/landscape "4:3",
  square "1:1") — the editor cuts the resolved crop to that exact ratio about its centre,
  so NEVER derive ratio tokens yourself; combine it with edge tokens when a specific
  region should be kept."##,
        top_level_only: false,
        video_only: false,
        capability: None,
    },
    OpDescriptor {
        name: "rotate",
        bullet: r##"- {"op":"rotate","dir":"left"|"right","times":1..3} — quarter turns only."##,
        top_level_only: false,
        video_only: false,
        capability: None,
    },
    OpDescriptor {
        name: "filter",
        bullet: r##"- {"op":"filter","mode":"none"|"bw"|"sepia"|"invert"|"contour"|"custom","tint":"#rrggbb"}
  — "custom" is a duotone tint and requires "tint"; "contour" is edge detection."##,
        top_level_only: false,
        video_only: false,
        capability: None,
    },
    OpDescriptor {
        name: "layout",
        bullet: r##"- {"op":"layout","lines":[{"points":[{"x":0,"y":0},...],"color":"#FFFF00","thickness":2,
  "pointSize":4,"style":"solid"|"dashed"|"dotted","locked":false,"fillColor":"transparent"}]}
  — draw annotation polylines in image-pixel coordinates. When asked to extract lines,
  shapes, or structure from an attached image, answer with this op."##,
        top_level_only: false,
        video_only: false,
        capability: None,
    },
    OpDescriptor {
        name: "formula",
        bullet: r##"- {"op":"formula","axis":"x"|"y","expr":"x*2+10"} — coordinate transform; single variable
  matching the axis; operators + - * / ** and parentheses only. An empty "expr" clears
  that axis; {"op":"formula","enabled":false} switches formulas OFF entirely."##,
        top_level_only: false,
        video_only: false,
        capability: None,
    },
    OpDescriptor {
        name: "page",
        bullet: r##"- {"op":"page","format":"a4"} — ISO page formats a0–a10, b0–b10, c0–c10 — or a custom
  size: {"op":"page","width":20,"height":30} in centimetres (one form or the other)."##,
        top_level_only: false,
        video_only: false,
        capability: None,
    },
    OpDescriptor {
        name: "blank",
        bullet: r##"- {"op":"blank","color":"#ffffff","format":"a4"} — create a blank page; explicit
  centimetre dims ride as "width"/"height" instead of "format"."##,
        top_level_only: false,
        video_only: false,
        capability: None,
    },
    OpDescriptor {
        name: "frame",
        bullet: r##"- {"op":"frame","index":0} or {"op":"frame","indices":[0,30,60]} — pick video frame(s);
  only valid when the current input is a video."##,
        top_level_only: false,
        video_only: true,
        capability: None,
    },
    OpDescriptor {
        name: "image",
        bullet: r##"- {"op":"image","index":1} — switch the working image to the Nth image attached to THIS
  message (1-based, in attachment order); coordinates in later actions are in THAT
  image's pixel frame. Only valid when the user attached images. Use it to edit several
  attached images in one plan, giving each image its OWN actions."##,
        top_level_only: true,
        video_only: false,
        capability: None,
    },
    OpDescriptor {
        name: "save",
        bullet: r##"- {"op":"save","name":"portrait 1"} — save the current image with its drawn lines as a
  project. When the user asks to process several images and keep the results, finish
  each image's actions with a "save" before switching to the next: image 1, its edits,
  save, image 2, its edits, save, …"##,
        top_level_only: true,
        video_only: false,
        capability: None,
    },
];

/// §13's never-model-drivable boundary, as op names: llm/provider self-configuration,
/// clipboard READS, hotkey rebinding, session/window end, chat persistence/consent
/// toggles, and server-side destruction beyond what §10 grants. Two teeth: no registry
/// entry may use one of these names (tested + rejected at assembly), and the plan
/// validator hard-fails any plan naming one instead of skipping it as unknown.
pub const FORBIDDEN_OPS: &[&str] = &[
    // llm/provider configuration — self-configuration is the exfiltration primitive
    "llm",
    "provider",
    "apiKey",
    "configureLlm",
    // clipboard reads (copied secrets would enter vision turns)
    "paste",
    // hotkey rebinding
    "hotkey",
    "rebind",
    // ending the session/window
    "quit",
    "exit",
    "closeWindow",
    "endSession",
    // chat persistence/clearing and consent toggles
    "chat",
    "chatPersist",
    "clearChat",
    "shareTabs",
    // server-side destruction beyond §10's explicit grants
    "deleteRemote",
    "removeRemote",
    "expireProject",
    "transferProject",
];

/// True when `name` sits on the §13 never-model-drivable boundary.
pub fn is_forbidden(name: &str) -> bool {
    FORBIDDEN_OPS.contains(&name)
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
