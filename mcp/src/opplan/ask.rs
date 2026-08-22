//! §11 interactive replies: validating the optional `ask` card and rendering it as text
//! for the calling agent.

use serde_json::Value;

use super::actions::misplaced_top_level_op;
use super::{
    AskCard, AskOption, OpPlanError, DEFAULT_CUSTOM_LABEL, MAX_ASK_LABEL, MAX_ASK_OPTIONS,
    MAX_ASK_QUESTION, MIN_ASK_OPTIONS,
};

/// Validate the optional `ask` object (contract §11) → the card, or `None` when absent.
/// Strict, like an action: a card nobody can answer (no options, one option, six options,
/// an option that is both a render and a reference) rejects the whole plan rather than
/// reaching the caller as a broken prompt.
pub(super) fn validate_ask(
    value: Option<&Value>,
    warnings: &mut Vec<String>,
) -> Result<Option<AskCard>, OpPlanError> {
    let Some(value) = value else { return Ok(None) };
    if value.is_null() {
        return Ok(None);
    }
    let Value::Object(ask) = value else {
        return Err(OpPlanError::Plan("\"ask\" must be an object".into()));
    };
    for key in ask.keys() {
        if !matches!(
            key.as_str(),
            "question" | "mode" | "options" | "allowCustom" | "customLabel"
        ) {
            return Err(OpPlanError::Plan(format!(
                "\"ask\" has an unknown field \"{key}\""
            )));
        }
    }

    let question = match ask.get("question") {
        Some(Value::String(s)) if !s.trim().is_empty() => s.trim().to_string(),
        _ => {
            return Err(OpPlanError::Plan(
                "\"ask.question\" must be a non-empty string".into(),
            ))
        }
    };
    if question.chars().count() > MAX_ASK_QUESTION {
        return Err(OpPlanError::Plan(format!(
            "\"ask.question\" is longer than {MAX_ASK_QUESTION} characters"
        )));
    }

    let multi = match ask.get("mode") {
        None | Some(Value::Null) => false,
        Some(Value::String(s)) if s == "multi" => true,
        Some(Value::String(s)) if s == "single" => false,
        _ => {
            return Err(OpPlanError::Plan(
                "\"ask.mode\" must be \"single\" or \"multi\"".into(),
            ))
        }
    };
    let allow_custom = match ask.get("allowCustom") {
        None | Some(Value::Null) => false,
        Some(Value::Bool(b)) => *b,
        _ => {
            return Err(OpPlanError::Plan(
                "\"ask.allowCustom\" must be a boolean".into(),
            ))
        }
    };
    let custom_label = match ask.get("customLabel") {
        None | Some(Value::Null) => DEFAULT_CUSTOM_LABEL.to_string(),
        Some(Value::String(s)) if !s.trim().is_empty() => {
            if s.chars().count() > MAX_ASK_LABEL {
                return Err(OpPlanError::Plan(format!(
                    "\"ask.customLabel\" is longer than {MAX_ASK_LABEL} characters"
                )));
            }
            s.trim().to_string()
        }
        _ => {
            return Err(OpPlanError::Plan(
                "\"ask.customLabel\" must be a non-empty string".into(),
            ))
        }
    };

    let Some(Value::Array(raw_options)) = ask.get("options") else {
        return Err(OpPlanError::Plan(
            "\"ask.options\" must be an array".into(),
        ));
    };
    if raw_options.len() < MIN_ASK_OPTIONS || raw_options.len() > MAX_ASK_OPTIONS {
        return Err(OpPlanError::Plan(format!(
            "\"ask.options\" must hold {MIN_ASK_OPTIONS}..{MAX_ASK_OPTIONS} options"
        )));
    }

    let mut options = Vec::with_capacity(raw_options.len());
    let mut dropped_preview = false;
    for (i, raw) in raw_options.iter().enumerate() {
        let position = i + 1;
        let Value::Object(option) = raw else {
            return Err(OpPlanError::Plan(format!(
                "ask option {position} must be an object"
            )));
        };
        for key in option.keys() {
            if !matches!(key.as_str(), "label" | "actions" | "image") {
                return Err(OpPlanError::Plan(format!(
                    "ask option {position} has an unknown field \"{key}\""
                )));
            }
        }
        let label = match option.get("label") {
            Some(Value::String(s)) if !s.trim().is_empty() => s.trim().to_string(),
            _ => {
                return Err(OpPlanError::Plan(format!(
                    "ask option {position} \"label\" must be a non-empty string"
                )))
            }
        };
        if label.chars().count() > MAX_ASK_LABEL {
            return Err(OpPlanError::Plan(format!(
                "ask option {position} \"label\" is longer than {MAX_ASK_LABEL} characters"
            )));
        }
        let has_actions = !matches!(option.get("actions"), None | Some(Value::Null));
        let has_image = !matches!(option.get("image"), None | Some(Value::Null));
        if has_actions && has_image {
            return Err(OpPlanError::Plan(format!(
                "ask option {position} carries both \"actions\" and \"image\" — an option \
                 previews a render OR names an existing image"
            )));
        }
        // §2.1 (registry flag): an option's preview may not switch images or save — those
        // are top-level actions. §1 leniency: the misplacement drops the PREVIEW with a
        // warning (the option keeps its place), it never fails the plan.
        if let Some(op) = misplaced_top_level_op(option.get("actions")) {
            warnings.push(format!(
                "Dropped the preview on ask option {position} (\"{label}\"): \"{op}\" is a \
                 top-level action only (§2.1)"
            ));
        }
        if has_actions || has_image {
            dropped_preview = true;
        }
        options.push(AskOption { label });
    }
    if dropped_preview {
        // One note for the whole card, not one per option: nothing here can show any of them.
        warnings.push(
            "note: option previews are not shown by this server — the choices are listed by name"
                .to_string(),
        );
    }

    Ok(Some(AskCard {
        question,
        multi,
        allow_custom,
        custom_label,
        options,
    }))
}

/// The card as text for the calling agent: the question, its numbered options, and how to
/// answer (by calling `stencil_prompt` again with the choice).
pub fn format_ask(card: &AskCard) -> String {
    let mut out = String::from("\n");
    out.push_str(&card.question);
    for (i, option) in card.options.iter().enumerate() {
        out.push_str(&format!("\n  {}. {}", i + 1, option.label));
    }
    if card.allow_custom {
        out.push_str(&format!("\n  or answer freely: {}", card.custom_label));
    }
    out.push_str(if card.multi {
        "\n(pick one or more, then send the choice as the next prompt)"
    } else {
        "\n(pick one, then send the choice as the next prompt)"
    });
    out
}
