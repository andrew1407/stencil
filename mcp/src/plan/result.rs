//! The CLI's `--plan-check` document (`cli/CONTRACT.md` §7) and its mapping onto [`OpPlan`]:
//! core has already validated and normalized every action, so this only types them, and its
//! warnings and error read in core's canonical words.

use serde::Deserialize;
use serde_json::Value;

use super::actions::lower;
use super::ask::{card, PREVIEWS_NOT_SHOWN};
use super::{OpPlan, OpPlanError, Variant};

pub type JsonObject = serde_json::Map<String, Value>;

/// The one line `--plan-check` prints: the envelope around core's result.
#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CheckDoc {
    pub version: u32,
    pub surface: String,
    pub registry_bytes: u64,
    pub registry_fnv1a64: String,
    pub result: PlanDoc,
}

/// Core's result document, byte-equal to `browser/js/llm/plan/parser.js` `walkPlan`.
#[derive(Debug, Deserialize)]
pub struct PlanDoc {
    pub status: String,
    #[serde(default)]
    pub reply: String,
    #[serde(default)]
    pub actions: Vec<JsonObject>,
    #[serde(default)]
    pub variants: Vec<VariantDoc>,
    #[serde(default)]
    pub ask: Option<AskDoc>,
    #[serde(default)]
    pub warnings: Vec<Notice>,
    #[serde(default)]
    pub error: Option<Notice>,
}

#[derive(Debug, Deserialize)]
pub struct VariantDoc {
    #[serde(default)]
    pub label: Option<String>,
    #[serde(default)]
    pub actions: Vec<JsonObject>,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AskDoc {
    pub question: String,
    #[serde(default)]
    pub mode: String,
    #[serde(default)]
    pub allow_custom: bool,
    #[serde(default)]
    pub custom_label: String,
    pub options: Vec<AskOptionDoc>,
}

#[derive(Debug, Deserialize)]
pub struct AskOptionDoc {
    pub label: String,
    #[serde(default)]
    pub actions: Option<Vec<JsonObject>>,
    #[serde(default)]
    pub image: Option<Value>,
}

/// A warning or the error: its code, the op it names, and core's detail and message.
#[derive(Debug, Default, Deserialize)]
#[serde(default)]
pub struct Notice {
    pub code: String,
    pub op: Option<String>,
    pub detail: String,
    pub message: String,
}

/// Core's error as core words it; an `E_ACTION` keeps its op.
fn refusal(n: Notice) -> OpPlanError {
    let message = match n.message.is_empty() {
        true => format!("Invalid plan: {}", n.detail),
        false => n.message,
    };
    match n.code.as_str() {
        "E_ACTION" => OpPlanError::Action { op: n.op.unwrap_or_default(), message },
        _ => OpPlanError::Plan(message),
    }
}

/// A result document's JSON text → the typed plan (or the plan's error).
pub fn from_result(json: &str) -> Result<OpPlan, OpPlanError> {
    let doc = serde_json::from_str::<PlanDoc>(json)
        .map_err(|e| OpPlanError::Checker(format!("unreadable plan-check result: {e}")))?;
    map(doc)
}

pub(super) fn map(doc: PlanDoc) -> Result<OpPlan, OpPlanError> {
    let chat_only = match doc.status.as_str() {
        "valid" => false,
        "chatOnly" => true,
        "invalid" => return Err(refusal(doc.error.unwrap_or_default())),
        other => return Err(OpPlanError::Checker(format!("unknown plan status \"{other}\""))),
    };
    let lower_all = |list: &[JsonObject]| list.iter().map(lower).collect::<Result<Vec<_>, _>>();
    let actions = lower_all(&doc.actions)?;
    let variant = |v: &VariantDoc| {
        let label = v.label.clone().unwrap_or_default();
        Ok(Variant { label, actions: lower_all(&v.actions)? })
    };
    let variants = doc.variants.iter().map(variant).collect::<Result<Vec<_>, OpPlanError>>()?;
    let dropped = doc.warnings.iter().any(|w| w.code == "W_PREVIEW_DROPPED");
    let mut warnings: Vec<String> = doc.warnings.iter().map(|w| w.message.clone()).collect();
    let ask = match doc.ask {
        Some(ask) => {
            let (card, shown) = card(ask, dropped);
            if shown {
                // The ask's own notes come before the plan-wide reply note, as they are walked.
                let reply_note = doc.warnings.iter().position(|w| w.code == "W_REPLY_OMITTED");
                let at = reply_note.unwrap_or(warnings.len());
                warnings.insert(at, PREVIEWS_NOT_SHOWN.to_string());
            }
            Some(card)
        }
        None => None,
    };
    Ok(OpPlan { reply: doc.reply, actions, variants, warnings, chat_only, ask })
}
