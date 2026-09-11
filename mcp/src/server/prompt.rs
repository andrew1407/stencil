//! The `stencil_prompt` flow: one LLM turn, its validated op-plan executed through the
//! CLI pipeline, wrapped in the contract-§7 auto-continuation loop — plus the payload
//! types and response assembly that shape the tool's reply.

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;
use serde::Serialize;

use crate::args::PromptParams;
use crate::config::Config;
use crate::llm::{self, ChatMessage, Role};
use crate::llmtransport::{clip, LlmTransport, SNIPPET_LEN};
use crate::{opplan, pipeline};

use super::{err_result, ok_result};

/// One written result of a `stencil_prompt` plan: the base result (`label` null), a variant
/// (its sanitized label), or a §2.1 `save`'s `.stencil` project — a document, so it reports
/// no pixel dimensions.
#[derive(Serialize)]
pub(super) struct PromptResult {
    pub(super) label: Option<String>,
    pub(super) path: String,
    pub(super) width: Option<u32>,
    pub(super) height: Option<u32>,
}

/// The `stencil_prompt` structured payload: the LLM's chat reply, any non-fatal notes
/// (skipped unknown ops, attachment fallbacks), and the written results (empty for a
/// chat-only turn).
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

/// The single success exit of `stencil_prompt`: the chat reply, any notes, one `wrote`
/// line per written result (none on a chat-only turn), and — since a plan may both edit
/// and ask (§11.3) — the ask card riding out last.
fn prompt_response(
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

/// A round-2 failure never costs round 1's work: fold the failure into a note and answer
/// with what the first round already wrote. With no first round it stays a hard error.
fn kept_or_error(
    first: Option<(String, Vec<PromptResult>)>,
    mut notes: Vec<String>,
    detail: String,
) -> Result<CallToolResult, McpError> {
    let Some((reply, results)) = first else {
        return Ok(err_result(detail));
    };
    notes.push(format!(
        "note: auto-continuation failed ({detail}) — kept the loaded image"
    ));
    let plan = opplan::OpPlan {
        reply,
        actions: Vec::new(),
        variants: Vec::new(),
        warnings: Vec::new(),
        chat_only: false,
        ask: None,
    };
    prompt_response(&plan, &notes, &results)
}

/// Fold a §7 round-1 outcome (when there was one) into the final response: the replies
/// join in order, and round 1's written files survive unless round 2 rewrote the path.
fn merged_response(
    first: Option<(String, Vec<PromptResult>)>,
    plan: &opplan::OpPlan,
    notes: &[String],
    results: Vec<PromptResult>,
) -> Result<CallToolResult, McpError> {
    let Some((first_reply, first_results)) = first else {
        return prompt_response(plan, notes, &results);
    };
    let mut merged: Vec<PromptResult> = first_results
        .into_iter()
        .filter(|r| !results.iter().any(|n| n.path == r.path))
        .collect();
    merged.extend(results);
    let reply = [first_reply.as_str(), plan.reply.as_str()]
        .iter()
        .filter(|part| !part.trim().is_empty())
        .copied()
        .collect::<Vec<_>>()
        .join("\n");
    let plan = opplan::OpPlan {
        reply,
        ..plan.clone()
    };
    prompt_response(&plan, notes, &merged)
}

/// The whole `stencil_prompt` flow: one LLM turn, its validated op-plan executed through
/// the CLI pipeline — wrapped in the contract-§7 auto-continuation loop: a plan that only
/// LOADED a picture (`opplan::loads_without_tracing`) is applied and the turn re-sent
/// exactly once with the freshly rendered result attached (plus its edge map), the
/// follow-up plan executed normally, whatever it contains — round 2 never continues
/// again, so a second load-only answer cannot loop. Public with the transport injected so
/// tests drive the flow over a mock.
pub async fn run_prompt(
    config: &Config,
    transport: std::sync::Arc<dyn LlmTransport>,
    params: PromptParams,
) -> Result<CallToolResult, McpError> {
    let PromptParams {
        prompt,
        input,
        output_dir,
        model,
    } = params;

    // Resolve the provider config: the STENCIL_LLM_* env, with `model` as the only
    // per-call override. The endpoint stays operator-configured on purpose — a
    // caller-supplied base URL would redirect STENCIL_LLM_API_KEY to any host.
    let settings = match llm::LlmConfig::resolve(&config.llm, model.as_deref()) {
        Ok(settings) => settings,
        Err(message) => return Ok(err_result(message)),
    };
    if output_dir.trim().is_empty() {
        return Ok(err_result("`output_dir` must not be empty".to_string()));
    }

    let mut notes: Vec<String> = Vec::new();
    // §7 continuation state: round 2 re-sends the SAME prompt (plus the note) over the
    // image round 1 rendered; `first` keeps round 1's reply + written results.
    let mut text = prompt.clone();
    let mut round_input = input;
    let mut first: Option<(String, Vec<PromptResult>)> = None;

    for round in 0..2 {
        // Attach a local input image for vision (all three providers accept images).
        let mut images = Vec::new();
        let mut system_suffix = String::new();
        if let Some(input) = &round_input {
            let (attachment, note) = llm::attach_local_image(input);
            let snapshot_attached = attachment.is_some();
            if let Some(attachment) = attachment {
                images.push(attachment);
            }
            if let Some(note) = note {
                notes.push(note);
            }
            // §7 edge map: a contour render of the input, right after the snapshot and only
            // when the snapshot rides; a missing CLI or failed/oversized render just skips it.
            if snapshot_attached {
                if let Some(edge) = pipeline::render_edge_map(input)
                    .await
                    .as_deref()
                    .and_then(llm::edge_map_attachment)
                {
                    images.push(edge);
                    system_suffix = llm::edge_map_suffix().to_string();
                }
            }
        }
        let messages = vec![ChatMessage {
            role: Role::User,
            text: text.clone(),
            images,
        }];

        // The transport is deliberately synchronous (std::net + OS timeouts); run it on
        // the blocking pool so the stdio protocol loop stays responsive.
        let chat_settings = settings.clone();
        let chat_transport = transport.clone();
        let chat = tokio::task::spawn_blocking(move || {
            llm::chat(
                chat_transport.as_ref(),
                &chat_settings,
                &messages,
                &system_suffix,
            )
        })
        .await;
        let reply = match chat {
            Ok(Ok(reply)) => reply,
            Ok(Err(error)) => {
                // The error already says the reason once (§6.3) — no preamble around it.
                return kept_or_error(first, notes, error.to_string());
            }
            Err(join_error) => {
                return kept_or_error(first, notes, format!("the LLM request could not be run: {join_error}"));
            }
        };

        // Parse + validate the op-plan (contract §1–§3); unknown ops become notes.
        let mut plan = match opplan::parse_op_plan(&reply) {
            Ok(plan) => plan,
            Err(error) => {
                return kept_or_error(
                    first,
                    notes,
                    format!(
                        "the LLM returned an invalid op-plan: {error}\nraw reply:\n{}",
                        clip(&reply, SNIPPET_LEN)
                    ),
                );
            }
        };
        notes.append(&mut plan.warnings);

        // Chat-only turn: return the text, write nothing. A turn that only ASKS lands here.
        if plan.actions.is_empty() && plan.variants.is_empty() {
            return merged_response(first, &plan, &notes, Vec::new());
        }

        // Execute: one CLI run for the base result, one per variant. Plan coordinates
        // are snapshot-frame (contract §1), so layout-drawing runs pass the CLI
        // `--layout-frame source` to re-map them through the run's crop/rotate.
        let requests = match opplan::to_edit_requests(
            &plan,
            round_input.as_deref(),
            &output_dir,
            &mut notes,
        ) {
            Ok(requests) => requests,
            Err(error) => return kept_or_error(first, notes, error.to_string()),
        };
        // A plan that was ONLY §2.1 ops this turn cannot satisfy (a second attachment that
        // does not exist here) leaves nothing to run — the notes above already say why.
        if requests.is_empty() {
            return merged_response(first, &plan, &notes, Vec::new());
        }
        if let Err(error) = std::fs::create_dir_all(&output_dir) {
            return kept_or_error(
                first,
                notes,
                format!("could not create output_dir '{output_dir}': {error}"),
            );
        }
        // A §10 save `path` may nest inside output_dir — create each output's parent.
        for request in &requests {
            if let Some(parent) = std::path::Path::new(&request.params.output).parent() {
                if let Err(error) = std::fs::create_dir_all(parent) {
                    return kept_or_error(
                        first,
                        notes,
                        format!("could not create '{}': {error}", parent.display()),
                    );
                }
            }
        }
        // The runs are independent — each variant replays the base actions from the original
        // input onto its own deduped path — so they run concurrently, joined before the reply;
        // results keep request order, and the failure reported is the first in that order.
        let mut handles = Vec::with_capacity(requests.len());
        for request in requests {
            let params = request.params;
            let project = request.project;
            handles.push((
                request.label,
                tokio::spawn(async move {
                    // A §2.1 `save` writes a `.stencil` document (no dimensions reported).
                    if project {
                        pipeline::run_project(&params)
                            .await
                            .map(|path| (path, None, None))
                    } else {
                        pipeline::run_edit(&params)
                            .await
                            .map(|r| (r.path, Some(r.width), Some(r.height)))
                    }
                }),
            ));
        }
        let mut outcomes = Vec::with_capacity(handles.len());
        for (label, handle) in handles {
            outcomes.push((label, handle.await));
        }
        let mut results: Vec<PromptResult> = Vec::new();
        let mut failed: Option<String> = None;
        for (label, outcome) in outcomes {
            // Flatten the JoinError (panic/cancel) and the run error into one path.
            let outcome = match outcome {
                Ok(run) => run.map_err(|e| e.to_string()),
                Err(join_error) => Err(join_error.to_string()),
            };
            match outcome {
                Ok((path, width, height)) => results.push(PromptResult {
                    label,
                    path,
                    width,
                    height,
                }),
                Err(error) => {
                    let what = label.as_deref().unwrap_or("the base result");
                    failed = Some(format!("executing the plan failed at {what}: {error}"));
                    break;
                }
            }
        }
        if let Some(detail) = failed {
            return kept_or_error(first, notes, detail);
        }

        // §7 auto-continuation: a plan that only loaded a picture cannot have finished
        // the looking-work — the snapshot rode along before it existed. Re-send the turn
        // once over the rendered base result; round 2 answers here whatever it planned.
        if round == 0 && opplan::loads_without_tracing(&plan) {
            if let Some(base) = results.iter().find(|r| r.label.is_none() && r.width.is_some()) {
                round_input = Some(base.path.clone());
                // §7 auto-continuation: the cli console's wording — this tool is
                // single-turn like it, so no history carries the request.
                let note = llm::prompt_field("continuationNoteConsole");
                text = format!("{prompt}\n\n{note}");
                first = Some((plan.reply.clone(), results));
                continue;
            }
        }
        return merged_response(first, &plan, &notes, results);
    }
    unreachable!("the §7 continuation loop answers by round 2")
}

#[cfg(test)]
mod tests {
    //! The `stencil_prompt` result surface (pure). These payload types ARE the contract
    //! with a calling agent; assertions run against the real serialized wire shape via
    //! the shared [`crate::server::testwire`] helpers.

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
