//! The four steps one `stencil_prompt` round takes: attach, chat, prepare, execute.
//! Each reports its failure as a ready-made detail string; the caller decides whether that
//! ends the turn or folds into round 1's results.

use crate::llm::{self, ChatMessage, ImageAttachment, LlmConfig, Role};
use crate::llmtransport::LlmTransport;
use crate::{opplan, pipeline};

use super::response::PromptResult;

/// Attach the round's input image for vision, plus the §7 edge map when one rendered.
/// Returns the attachments and the §4 system suffix the edge map requires.
pub(super) async fn attach(
    round_input: &Option<String>,
    notes: &mut Vec<String>,
) -> (Vec<ImageAttachment>, String) {
    let mut images = Vec::new();
    let mut system_suffix = String::new();
    if let Some(input) = &round_input {
        // Independent, so run together: the snapshot read and the edge-map CLI render.
        let local = std::path::Path::new(input.as_str()).is_file();
        let owned = input.clone();
        let read = tokio::task::spawn_blocking(move || llm::attach_local_image(&owned));
        let render = async { if local { pipeline::render_edge_map(input).await } else { None } };
        let (read, edge_map) = tokio::join!(read, render);
        let (attachment, note) = read.unwrap_or((None, None));
        let snapshot_attached = attachment.is_some();
        if let Some(attachment) = attachment {
            images.push(attachment);
        }
        if let Some(note) = note {
            notes.push(note);
        }
        // §7 edge map: it rides right after the snapshot and only alongside one; a
        // missing CLI or a failed/oversized render just skips it.
        if snapshot_attached {
            if let Some(edge) = edge_map.as_deref().and_then(llm::edge_map_attachment) {
                images.push(edge);
                system_suffix = llm::edge_map_suffix().to_string();
            }
        }
    }
    (images, system_suffix)
}

/// One chat round. The transport is deliberately synchronous (std::net + OS timeouts); run
/// it on the blocking pool so the stdio protocol loop stays responsive.
pub(super) async fn chat_once(
    transport: &std::sync::Arc<dyn LlmTransport>,
    settings: &LlmConfig,
    text: &str,
    images: Vec<ImageAttachment>,
    system_suffix: String,
) -> Result<String, String> {
    let messages = vec![ChatMessage {
        role: Role::User,
        text: text.to_string(),
        images,
    }];
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
    match chat {
        // The error already says the reason once (§6.3) — no preamble around it.
        Ok(result) => result.map_err(|error| error.to_string()),
        Err(join_error) => Err(format!("the LLM request could not be run: {join_error}")),
    }
}

/// Lower the plan into CLI runs and make every directory they write into. An empty result
/// means the plan left nothing to run this turn — the notes already say why.
pub(super) async fn prepare_outputs(
    plan: &opplan::OpPlan,
    round_input: Option<&str>,
    output_dir: &str,
    notes: &mut Vec<String>,
) -> Result<Vec<opplan::EditRequest>, String> {
    // Plan coordinates are snapshot-frame (§1), so layout-drawing runs pass the CLI
    // `--layout-frame source` to re-map them through the run's crop/rotate.
    let requests = opplan::to_edit_requests(plan, round_input, output_dir, notes)
        .map_err(|error| error.to_string())?;
    // A plan that was ONLY §2.1 ops this turn cannot satisfy (a second attachment that
    // does not exist here) leaves nothing to run — the notes above already say why.
    if requests.is_empty() {
        return Ok(requests);
    }
    make_output_dirs(output_dir.to_string(), &requests).await?;
    Ok(requests)
}

/// `output_dir` plus each output's own parent (§10 saves nest), in one hop off the runtime.
async fn make_output_dirs(dir: String, requests: &[opplan::EditRequest]) -> Result<(), String> {
    let parents: Vec<std::path::PathBuf> = requests
        .iter()
        .filter_map(|r| std::path::Path::new(&r.params.output).parent().map(Into::into))
        .collect();
    let made = tokio::task::spawn_blocking(move || {
        std::fs::create_dir_all(&dir)
            .map_err(|e| format!("could not create output_dir '{dir}': {e}"))?;
        for parent in parents {
            std::fs::create_dir_all(&parent)
                .map_err(|e| format!("could not create '{}': {e}", parent.display()))?;
        }
        Ok(())
    })
    .await;
    made.unwrap_or_else(|join_error| Err(format!("could not create output_dir: {join_error}")))
}

/// Run every request, joined before the reply, against the real CLI.
pub(super) async fn execute_concurrently(
    requests: Vec<opplan::EditRequest>,
) -> Result<Vec<PromptResult>, String> {
    execute_plan(std::sync::Arc::new(pipeline::ProcessRunner), requests).await
}

/// The fan-out, generic over the runner so a suite drives it without a CLI binary; the runs
/// are independent and the results come back in request order. A `JoinSet`, never a bare
/// `tokio::spawn`: dropping it aborts every run it still holds, so a cancelled call — and the
/// first failure — kills the CLI children instead of leaving them to finish detached.
pub async fn execute_plan<R: pipeline::CliRunner + Send + 'static>(
    runner: std::sync::Arc<R>,
    requests: Vec<opplan::EditRequest>,
) -> Result<Vec<PromptResult>, String> {
    let mut runs = tokio::task::JoinSet::new();
    let mut labels: Vec<Option<String>> = Vec::with_capacity(requests.len());
    for (index, request) in requests.into_iter().enumerate() {
        labels.push(request.label);
        let runner = runner.clone();
        let params = request.params;
        let project = request.project;
        runs.spawn(async move {
            // A §2.1 `save` writes a `.stencil` document (no dimensions reported).
            let run = if project {
                pipeline::run::project(runner.as_ref(), &params)
                    .await
                    .map(|path| (path, None, None))
            } else {
                pipeline::run::edit(runner.as_ref(), &params)
                    .await
                    .map(|r| (r.path, Some(r.width), Some(r.height)))
            };
            (index, run)
        });
    }

    let mut done: Vec<Option<(String, Option<u32>, Option<u32>)>> = vec![None; labels.len()];
    while let Some(joined) = runs.join_next().await {
        // A JoinError here is a panic: nothing cancels a run but this function returning.
        let (index, run) = joined.map_err(|e| format!("executing the plan failed: {e}"))?;
        match run {
            Ok(value) => done[index] = Some(value),
            Err(error) => {
                let what = labels[index].as_deref().unwrap_or("the base result");
                return Err(format!("executing the plan failed at {what}: {error}"));
            }
        }
    }
    Ok(labels
        .into_iter()
        .zip(done)
        .map(|(label, run)| {
            let (path, width, height) = run.expect("every run reported before the set drained");
            PromptResult { label, path, width, height }
        })
        .collect())
}
