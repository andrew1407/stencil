//! The §7 round merges: how a round-2 outcome folds onto what round 1 already wrote.

use rmcp::model::CallToolResult;
use rmcp::ErrorData as McpError;

use crate::opplan;
use crate::server::tools::err_result;

use super::response::{prompt_response, PromptResult};

/// A round-2 failure never costs round 1's work: fold the failure into a note and answer
/// with what the first round already wrote. With no first round it stays a hard error.
pub(super) fn kept_or_error(
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
pub(super) fn merged_response(
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

#[cfg(test)]
mod tests {
    //! The §7 merges (pure), asserted on the real serialized wire shape.

    use super::*;
    use crate::server::testwire::{payload_of, summary_of, wire};

    fn plan(reply: &str) -> opplan::OpPlan {
        opplan::OpPlan {
            reply: reply.into(),
            actions: vec![],
            variants: vec![],
            warnings: vec![],
            chat_only: true,
            ask: None,
        }
    }

    fn wrote(path: &str) -> PromptResult {
        PromptResult { label: None, path: path.into(), width: Some(2), height: Some(3) }
    }

    #[test]
    fn a_round_two_failure_with_no_first_round_stays_a_hard_error() {
        let result = kept_or_error(None, vec![], "the model refused".into()).unwrap();
        let wire = wire(&result);
        assert_eq!(wire["isError"], true);
        assert_eq!(wire["content"][0]["text"], "the model refused");
    }

    #[test]
    fn a_round_two_failure_keeps_round_ones_work_as_a_note() {
        let first = Some(("Loaded it.".to_string(), vec![wrote("/tmp/a.png")]));
        let result = kept_or_error(first, vec![], "upstream timed out".into()).unwrap();

        assert_eq!(wire(&result)["isError"], false, "round 1's work must still be reported");
        assert_eq!(
            summary_of(&result),
            "Loaded it.\nnote: auto-continuation failed (upstream timed out) \
             — kept the loaded image\nwrote /tmp/a.png (2x3)"
        );
        let payload = payload_of(&result);
        assert_eq!(payload["reply"], "Loaded it.");
        assert_eq!(payload["results"][0]["path"], "/tmp/a.png");
    }

    #[test]
    fn with_no_first_round_the_merge_is_just_the_plain_response() {
        let results = vec![wrote("/tmp/b.png")];
        let result = merged_response(None, &plan("Done."), &[], results).unwrap();
        assert_eq!(summary_of(&result), "Done.\nwrote /tmp/b.png (2x3)");
    }

    #[test]
    fn the_replies_join_in_round_order_and_round_ones_files_survive() {
        let first = Some(("Loaded it.".to_string(), vec![wrote("/tmp/a.png")]));
        let result =
            merged_response(first, &plan("Then cropped."), &[], vec![wrote("/tmp/b.png")]).unwrap();

        assert_eq!(
            summary_of(&result),
            "Loaded it.\nThen cropped.\nwrote /tmp/a.png (2x3)\nwrote /tmp/b.png (2x3)"
        );
        assert_eq!(payload_of(&result)["reply"], "Loaded it.\nThen cropped.");
    }

    /// Round 2 rewriting the same path must not report it twice.
    #[test]
    fn a_rewritten_path_is_reported_once_from_round_two() {
        let first = Some(("First.".to_string(), vec![wrote("/tmp/same.png")]));
        let mut second = wrote("/tmp/same.png");
        second.width = Some(40);
        let result = merged_response(first, &plan("Second."), &[], vec![second]).unwrap();

        assert_eq!(summary_of(&result), "First.\nSecond.\nwrote /tmp/same.png (40x3)");
        assert_eq!(payload_of(&result)["results"].as_array().unwrap().len(), 1);
    }

    /// An empty round-1 reply must not leave a leading blank line in the joined reply.
    #[test]
    fn an_empty_round_reply_is_dropped_from_the_join() {
        let first = Some(("   ".to_string(), vec![]));
        let result = merged_response(first, &plan("Only this."), &[], vec![]).unwrap();
        assert_eq!(summary_of(&result), "Only this.");
    }
}
