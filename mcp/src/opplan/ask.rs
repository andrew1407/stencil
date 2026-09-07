//! §11 interactive replies: validating the optional `ask` card and rendering it as text
//! for the calling agent.

use serde_json::Value;

use super::actions::{misplaced_top_level_op, validate_actions};
use super::schema::schema;
use super::{AskCard, AskOption, OpPlanError};

/// Validate the optional `ask` object (contract §11) → the card, or `None` when absent.
/// The card's structure (keys, caps, the image reference's exactly-one-of url /
/// projectId / scanIndex, http(s)-only urls) is the registry's ask schema; a card nobody
/// can answer rejects the whole plan rather than reaching the caller as a broken prompt.
pub(super) fn validate_ask(
    value: Option<&Value>,
    warnings: &mut Vec<String>,
) -> Result<Option<AskCard>, OpPlanError> {
    let Some(value) = value.filter(|v| !v.is_null()) else {
        return Ok(None);
    };
    let schema = schema();
    schema.validate_ask(value).map_err(OpPlanError::Plan)?;
    // As a `Value`, so an absent optional key reads as `Null` instead of panicking.
    let card =
        Value::Object(schema.normalize_ask(value.as_object().expect("validated as an object")));

    let mut options = Vec::new();
    let mut dropped_preview = false;
    for (i, option) in card["options"].as_array().into_iter().flatten().enumerate() {
        let position = i + 1;
        let label = option["label"].as_str().unwrap_or_default().to_string();
        let has_actions = !option["actions"].is_null();
        let has_image = !option["image"].is_null();
        // Preview actions are ordinary §2 actions. §2.1 (registry flag): a preview may
        // not switch images or save; §1 leniency: the misplacement drops the PREVIEW with
        // a warning (the option keeps its place), it never fails the plan.
        if has_actions {
            if let Some(op) = misplaced_top_level_op(option.get("actions")) {
                warnings.push(format!(
                    "Dropped the preview on ask option {position} (\"{label}\"): \"{op}\" is a \
                     top-level action only (§2.1)"
                ));
            } else {
                validate_actions(
                    option.get("actions"),
                    warnings,
                    &format!("ask option {position} actions"),
                )?;
            }
        }
        // This server is a TOOL, not a chat: it cannot show a picture, so the preview —
        // a render spec or an image reference — is dropped and only the label survives.
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
        question: card["question"].as_str().unwrap_or_default().to_string(),
        multi: card["mode"].as_str() == Some("multi"),
        allow_custom: card["allowCustom"].as_bool().unwrap_or(false),
        custom_label: card["customLabel"]
            .as_str()
            .unwrap_or_else(|| schema.default_custom_label())
            .to_string(),
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
