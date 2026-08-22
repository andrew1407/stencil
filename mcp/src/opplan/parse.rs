//! Extraction and plan-shape validation (contract §1): fences, the first balanced JSON
//! object, the chat-only fallback, and the top-level `reply`/`actions`/`variants` walk.

use serde_json::Value;

use super::actions::{misplaced_top_level_op, validate_actions};
use super::ask::validate_ask;
use super::{OpPlan, OpPlanError, Variant, MAX_LABEL_CHARS, MAX_STRING_CHARS, MAX_VARIANTS};

/// Remove Markdown code fences (``` with an optional language tag), keeping the rest of
/// the text intact — the JS reference's `raw.replace(/```[a-zA-Z]*/g, '')`.
fn strip_fences(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    let mut rest = text;
    while let Some(i) = rest.find("```") {
        out.push_str(&rest[..i]);
        rest = &rest[i + 3..];
        let tag_len = rest.chars().take_while(|c| c.is_ascii_alphabetic()).count();
        rest = &rest[tag_len..];
    }
    out.push_str(rest);
    out
}

/// The first balanced `{ … }` slice (string- and escape-aware), or `None`.
fn first_json_object(text: &str) -> Option<&str> {
    let start = text.find('{')?;
    let mut depth = 0usize;
    let mut in_string = false;
    let mut escaped = false;
    for (i, c) in text[start..].char_indices() {
        if in_string {
            if escaped {
                escaped = false;
            } else if c == '\\' {
                escaped = true;
            } else if c == '"' {
                in_string = false;
            }
            continue;
        }
        match c {
            '"' => in_string = true,
            '{' => depth += 1,
            '}' => {
                depth -= 1;
                if depth == 0 {
                    return Some(&text[start..start + i + c.len_utf8()]);
                }
            }
            _ => {}
        }
    }
    None
}

/// Name a variant for a warning: its 1-based position plus its label, when it has one
/// (clipped — a label is free-form model text).
fn variant_name(index: usize, label: &str) -> String {
    let label = label.trim();
    if label.is_empty() {
        return format!("variant {}", index + 1);
    }
    let clipped: String = label.chars().take(MAX_LABEL_CHARS).collect();
    format!("variant {} (\"{clipped}\")", index + 1)
}

/// Parse the raw LLM reply into a validated plan. Text with no JSON object — or braces
/// that aren't actually JSON — is a chat-only turn (raw text = `reply`, zero actions, not
/// an error). A found JSON object is validated strictly and invalid plans error out.
pub fn parse_op_plan(text: &str) -> Result<OpPlan, OpPlanError> {
    let chat_only = || OpPlan {
        reply: text.trim().to_string(),
        actions: Vec::new(),
        variants: Vec::new(),
        warnings: Vec::new(),
        chat_only: true,
        ask: None,
    };

    let stripped = strip_fences(text);
    let Some(candidate) = first_json_object(&stripped) else {
        return Ok(chat_only());
    };
    let Ok(value) = serde_json::from_str::<Value>(candidate) else {
        return Ok(chat_only()); // braces that aren't actually JSON → chat-only
    };
    let Value::Object(object) = value else {
        return Ok(chat_only());
    };

    // `version` other than 1 (or absent) is accepted but ignored (contract §1).
    let mut warnings = Vec::new();
    // §1 reply tolerance: models routinely omit the reply while planning valid
    // actions — substitute rather than lose the plan to a missing pleasantry.
    // The substitute itself is chosen below, once the plan's contents are known.
    let reply = match object.get("reply") {
        Some(Value::String(s)) if !s.trim().is_empty() => Some(s.clone()),
        _ => None,
    };
    let actions = validate_actions(object.get("actions"), &mut warnings, "\"actions\"")?;

    let raw_variants: &[Value] = match object.get("variants") {
        None | Some(Value::Null) => &[],
        Some(Value::Array(list)) => list,
        Some(_) => {
            return Err(OpPlanError::Plan("\"variants\" must be an array".to_string()));
        }
    };
    if raw_variants.len() > MAX_VARIANTS {
        return Err(OpPlanError::Plan(format!(
            "more than {MAX_VARIANTS} variants"
        )));
    }
    let mut variants = Vec::with_capacity(raw_variants.len());
    for (i, raw) in raw_variants.iter().enumerate() {
        let Value::Object(v) = raw else {
            return Err(OpPlanError::Plan("every variant must be an object".to_string()));
        };
        let position = format!("variant {}", i + 1);
        // An absent/empty label stays empty here; `to_edit_requests` falls back to the
        // positional `variant-N` file stem (like the other clients).
        let label = match v.get("label") {
            None | Some(Value::Null) => String::new(),
            Some(Value::String(s)) if s.len() <= MAX_STRING_CHARS => s.clone(),
            Some(_) => {
                return Err(OpPlanError::Plan("variant \"label\" must be a string".to_string()));
            }
        };
        // §1 leniency: a variant that misplaces a top-level-only op costs THAT variant its
        // place, not the whole turn's work — drop it with a warning and run the rest.
        if let Some(op) = misplaced_top_level_op(v.get("actions")) {
            warnings.push(format!(
                "Dropped {}: \"{op}\" is a top-level action only (§2.1) — not allowed inside \
                 variants",
                variant_name(i, &label)
            ));
            continue;
        }
        let actions = validate_actions(v.get("actions"), &mut warnings, &position)?;
        variants.push(Variant { label, actions });
    }

    let ask = validate_ask(object.get("ask"), &mut warnings)?;

    // "Done." only when the plan actually carries work — a bare "Done." on an
    // empty plan reads as a success that never occurred (contract §1).
    let reply = reply.unwrap_or_else(|| {
        if !actions.is_empty() || !variants.is_empty() || ask.is_some() {
            warnings.push("The model omitted its reply — the plan still ran".to_string());
            "Done.".to_string()
        } else {
            "The model returned an empty plan — nothing was changed.".to_string()
        }
    });

    Ok(OpPlan {
        reply,
        actions,
        variants,
        warnings,
        chat_only: false,
        ask,
    })
}
