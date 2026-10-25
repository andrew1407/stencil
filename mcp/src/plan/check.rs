//! The model's reply judged by core: handed on stdin to the CLI's `--plan-check - --plan-surface
//! mcp` (`cli/CONTRACT.md` §7) through the one [`CliRunner`], so the slot pool, the scrubbed
//! environment and the deadline apply; the document it prints is mapped onto [`OpPlan`].

use std::borrow::Cow;

use super::result::{map, CheckDoc};
use super::{OpPlan, OpPlanError};
use crate::outcome;
use crate::pipeline::{CliRunner, ProcessRunner};
use crate::registry;

/// The argv of every check; the reply rides on stdin.
pub const PLAN_CHECK_ARGV: [&str; 4] = ["--plan-check", "-", "--plan-surface", registry::SURFACE];

/// The envelope version this adapter reads.
const VERSION: u32 = 1;

/// Judge a raw model reply: a valid plan, a chat-only turn (no JSON object), or the reason the
/// plan is invalid; a CLI that cannot judge it is `OpPlanError::Checker`.
pub async fn parse_op_plan(text: &str) -> Result<OpPlan, OpPlanError> {
    parse_op_plan_with(&ProcessRunner, text).await
}

/// [`parse_op_plan`] over any runner, so a suite drives it without a binary.
pub async fn parse_op_plan_with<R: CliRunner>(
    runner: &R,
    text: &str,
) -> Result<OpPlan, OpPlanError> {
    let doc = check(runner, text).await.map_err(OpPlanError::Checker)?;
    if let Some(skew) = skew(&doc) {
        return Err(OpPlanError::Checker(skew));
    }
    map(doc.result)
}

/// The startup probe: `None` when the CLI judges plans against this server's registry, else
/// the warning to log — a missing CLI, or one built from another `opRegistry.json`.
pub async fn registry_skew<R: CliRunner>(runner: &R) -> Option<String> {
    match check(runner, "").await {
        Ok(doc) => skew(&doc),
        Err(detail) => Some(format!("stencil_prompt cannot check plans: {detail}")),
    }
}

async fn check<R: CliRunner>(runner: &R, text: &str) -> Result<CheckDoc, String> {
    let argv: Vec<Cow<'static, str>> = PLAN_CHECK_ARGV.iter().map(|a| Cow::Borrowed(*a)).collect();
    let output = runner.run_feeding(&argv, text).await?;
    match serde_json::from_str::<CheckDoc>(output.stdout.trim()) {
        Ok(doc) if doc.version == VERSION && doc.surface == registry::SURFACE => Ok(doc),
        Ok(doc) => Err(format!(
            "the stencil CLI answered a version {} check for \"{}\"; this server reads version \
             {VERSION} for \"{}\"",
            doc.version,
            doc.surface,
            registry::SURFACE
        )),
        Err(_) if !output.success => Err(outcome::extract_errors(&output.stderr)),
        Err(e) => Err(format!("the stencil CLI printed no plan-check document ({e})")),
    }
}

/// The CLI embeds its own `opRegistry.json`; a different one judges by different rules.
fn skew(doc: &CheckDoc) -> Option<String> {
    let (bytes, fnv) = registry::fingerprint();
    if doc.registry_bytes == bytes && doc.registry_fnv1a64 == fnv {
        return None;
    }
    Some(format!(
        "the stencil CLI was built from a different opRegistry.json ({} bytes, fnv1a64 {}) than \
         this server ({bytes} bytes, fnv1a64 {fnv}) — rebuild both from one checkout; plans are \
         refused until then",
        doc.registry_bytes, doc.registry_fnv1a64
    ))
}
