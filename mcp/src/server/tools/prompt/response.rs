//! What a `stencil_prompt` turn reports back: the payload types and the single
//! success exit. The §7 round merges are in `merge.rs`.

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;
use serde::Serialize;

use crate::opplan;
use crate::server::tools::ok_result;

/// One written result of a `stencil_prompt` plan: the base result (`label` null), a variant
/// (its sanitized label), or a §2.1 `save`'s `.stencil` — a document, so no dimensions.
#[derive(Serialize)]
pub struct PromptResult {
    pub label: Option<String>,
    pub path: String,
    pub width: Option<u32>,
    pub height: Option<u32>,
}

/// The `stencil_prompt` structured payload: the chat reply, any non-fatal notes, and the
/// written results (empty for a chat-only turn).
#[derive(Serialize)]
struct PromptPayload<'a> {
    reply: &'a str,
    notes: &'a [String],
    results: &'a [PromptResult],
    /// The turn's question (contract §11), when it asked one. No interactive surface here:
    /// the agent gets the card as data and answers by calling `stencil_prompt` again.
    #[serde(skip_serializing_if = "Option::is_none")]
    ask: Option<PromptAsk<'a>>,
}

/// The `ask` card as the tool reports it: the question, whether several picks are allowed,
/// and the option LABELS (previews are not shown by this server — contract §11.4).
#[derive(Serialize)]
struct PromptAsk<'a> {
    question: &'a str,
    multi: bool,
    allow_custom: bool,
    options: Vec<&'a str>,
}

/// Borrow a validated card into the payload shape.
fn prompt_ask(card: &opplan::AskCard) -> PromptAsk<'_> {
    PromptAsk {
        question: &card.question,
        multi: card.multi,
        allow_custom: card.allow_custom,
        options: card.options.iter().map(|o| o.label.as_str()).collect(),
    }
}

/// The single success exit of `stencil_prompt`: the reply, any notes, one `wrote` line per
/// result, and — since a plan may both edit and ask (§11.3) — the ask card last.
pub(super) fn prompt_response(
    plan: &opplan::OpPlan,
    notes: &[String],
    results: &[PromptResult],
) -> Result<CallToolResult, McpError> {
    use std::fmt::Write;
    let mut summary = plan.reply.clone();
    for note in notes {
        let _ = write!(summary, "\n{note}");
    }
    for result in results {
        match (result.width, result.height) {
            (Some(width), Some(height)) => {
                let _ = write!(summary, "\nwrote {} ({width}x{height})", result.path);
            }
            // A §2.1 `save`: the CLI's own project line, verbatim.
            _ => {
                let _ = write!(summary, "\nwrote {} (project)", result.path);
            }
        }
    }
    if let Some(card) = &plan.ask {
        summary.push_str(&opplan::format_ask(card));
    }
    let payload = PromptPayload {
        reply: &plan.reply,
        notes,
        results,
        ask: plan.ask.as_ref().map(prompt_ask),
    };
    ok_result(summary, payload)
}

#[cfg(test)]
mod tests {
    //! The `stencil_prompt` result surface (pure). These payload types ARE the contract with a
    //! calling agent; assertions run against the real serialized wire shape.

    use super::*;
    use crate::opplan::{AskCard, AskOption, OpPlan};
    use crate::server::testwire::{payload_of, summary_of};
    use serde_json::{json, Value};

    fn plan(reply: &str) -> OpPlan {
        OpPlan {
            reply: reply.into(),
            actions: vec![],
            variants: vec![],
            warnings: vec![],
            chat_only: true,
            ask: None,
        }
    }

    fn card() -> AskCard {
        AskCard {
            question: "Which crop?".into(),
            multi: false,
            allow_custom: true,
            custom_label: "describe it".into(),
            options: vec![
                AskOption {
                    label: "Square".into(),
                },
                AskOption {
                    label: "Wide".into(),
                },
            ],
        }
    }

    // ── prompt_ask ──

    #[test]
    fn prompt_ask_reports_the_question_and_labels_only() {
        let card = card();
        let value = serde_json::to_value(prompt_ask(&card)).unwrap();
        assert_eq!(
            value,
            json!({
                "question": "Which crop?",
                "multi": false,
                "allow_custom": true,
                "options": ["Square", "Wide"],
            })
        );
        // §11.4: this server has no way to show a preview, so only labels travel — a
        // client answers by sending the chosen label back as the next prompt.
        assert!(value["options"].as_array().unwrap().iter().all(|o| o.is_string()));
    }

    // ── prompt_response ──

    #[test]
    fn a_chat_only_turn_is_just_the_reply() {
        let result = prompt_response(&plan("Rotated already, nothing to do."), &[], &[]).unwrap();

        assert_eq!(summary_of(&result), "Rotated already, nothing to do.");
        assert_eq!(
            payload_of(&result),
            json!({
                "reply": "Rotated already, nothing to do.",
                "notes": [],
                "results": [],
            }),
            "a turn with no question must omit `ask` entirely, not send null"
        );
    }

    #[test]
    fn notes_are_appended_to_the_summary_and_carried_structurally() {
        let notes = vec!["skipped unknown op \"warp\"".to_string(), "used a fallback".to_string()];
        let result = prompt_response(&plan("Done."), &notes, &[]).unwrap();

        assert_eq!(
            summary_of(&result),
            "Done.\nskipped unknown op \"warp\"\nused a fallback"
        );
        assert_eq!(payload_of(&result)["notes"], json!(notes));
        // The reply field stays clean — notes are additive, not folded into it.
        assert_eq!(payload_of(&result)["reply"], "Done.");
    }

    #[test]
    fn each_written_result_adds_the_cli_wrote_line() {
        let results = vec![
            PromptResult {
                label: None,
                path: "/tmp/out.png".into(),
                width: Some(800),
                height: Some(600),
            },
            PromptResult {
                label: Some("warm".into()),
                path: "/tmp/out-warm.png".into(),
                width: Some(800),
                height: Some(600),
            },
        ];
        let result = prompt_response(&plan("Two takes."), &[], &results).unwrap();

        assert_eq!(
            summary_of(&result),
            "Two takes.\nwrote /tmp/out.png (800x600)\nwrote /tmp/out-warm.png (800x600)"
        );
        let payload = payload_of(&result);
        assert_eq!(payload["results"][0]["label"], Value::Null, "the base result has no label");
        assert_eq!(payload["results"][1]["label"], "warm");
        assert_eq!(payload["results"][1]["path"], "/tmp/out-warm.png");
        assert_eq!(payload["results"][0]["width"], 800);
        assert_eq!(payload["results"][0]["height"], 600);
    }

    #[test]
    fn an_ask_card_rides_out_last_on_the_summary_and_as_data() {
        let mut p = plan("Two ways to crop this.");
        p.ask = Some(card());
        let result = prompt_response(&p, &[], &[]).unwrap();

        let summary = summary_of(&result);
        assert!(summary.starts_with("Two ways to crop this."));
        assert_eq!(
            summary,
            format!("Two ways to crop this.{}", crate::opplan::format_ask(&card())),
            "the summary's card must be exactly what format_ask renders"
        );
        assert!(summary.contains("1. Square") && summary.contains("2. Wide"));

        let payload = payload_of(&result);
        assert_eq!(payload["ask"]["question"], "Which crop?");
        assert_eq!(payload["ask"]["options"], json!(["Square", "Wide"]));
    }

    /// §2.1: a `save` result is a project document — reported without dimensions, and with
    /// null dims in the payload, so nobody reads a `.stencil` as a 0x0 image.
    #[test]
    fn a_saved_project_reports_the_cli_project_line() {
        let results = vec![
            PromptResult {
                label: None,
                path: "/tmp/result.png".into(),
                width: Some(64),
                height: Some(48),
            },
            PromptResult {
                label: Some("portrait-1".into()),
                path: "/tmp/portrait-1.stencil".into(),
                width: None,
                height: None,
            },
        ];
        let result = prompt_response(&plan("Saved it."), &[], &results).unwrap();

        assert_eq!(
            summary_of(&result),
            "Saved it.\nwrote /tmp/result.png (64x48)\nwrote /tmp/portrait-1.stencil (project)"
        );
        let payload = payload_of(&result);
        assert_eq!(payload["results"][1]["width"], Value::Null);
        assert_eq!(payload["results"][1]["label"], "portrait-1");
    }

    /// §11.3: a plan may both edit and ask. The card comes after the wrote lines.
    #[test]
    fn a_plan_that_edits_and_asks_reports_both_in_order() {
        let mut p = plan("Cropped it — which finish?");
        p.ask = Some(card());
        let results = vec![PromptResult {
            label: None,
            path: "/tmp/o.png".into(),
            width: Some(10),
            height: Some(20),
        }];
        let result = prompt_response(&p, &["a note".to_string()], &results).unwrap();

        let summary = summary_of(&result);
        let lines: Vec<&str> = summary.lines().collect();
        assert_eq!(lines[0], "Cropped it — which finish?");
        assert_eq!(lines[1], "a note", "notes come before the wrote lines");
        assert_eq!(lines[2], "wrote /tmp/o.png (10x20)");
        assert_eq!(lines[3], "Which crop?", "the card rides out last");

        let payload = payload_of(&result);
        assert_eq!(payload["results"].as_array().unwrap().len(), 1);
        assert!(payload["ask"].is_object());
    }
}
