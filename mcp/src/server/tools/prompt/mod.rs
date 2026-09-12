//! `stencil_prompt`: one LLM turn, its validated op-plan executed through the CLI
//! pipeline, wrapped in the contract-§7 auto-continuation loop.

mod execute;
mod merge;
mod response;

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;

use crate::args::PromptParams;
use crate::config::Config;
use crate::llm;
use crate::llmtransport::{clip, LlmTransport, PlainHttpTransport, SNIPPET_LEN};
use crate::opplan;
use crate::server::tools::err_result;

use execute::{attach, chat_once, execute_concurrently, prepare_outputs};
use merge::{kept_or_error, merged_response};
use response::PromptResult;

/// The tool's whole body: the real plain-http transport, then the flow below.
pub async fn run(config: &Config, params: PromptParams) -> Result<CallToolResult, McpError> {
    run_prompt(
        config,
        std::sync::Arc::new(PlainHttpTransport::new()),
        params,
    )
    .await
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
        let (images, system_suffix) = attach(&round_input, &mut notes).await;
        let reply = match chat_once(&transport, &settings, &text, images, system_suffix).await {
            Ok(reply) => reply,
            Err(detail) => return kept_or_error(first, notes, detail),
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
        let requests =
            match prepare_outputs(&plan, round_input.as_deref(), &output_dir, &mut notes).await {
                Ok(requests) => requests,
                Err(detail) => return kept_or_error(first, notes, detail),
            };
        if requests.is_empty() {
            return merged_response(first, &plan, &notes, Vec::new());
        }
        let results = match execute_concurrently(requests).await {
            Ok(results) => results,
            Err(detail) => return kept_or_error(first, notes, detail),
        };

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
