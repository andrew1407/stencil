//! Contract §11 interactive replies (`ask`) and the §7 auto-continuation rule.

mod common;

use stencil_mcp::opplan::{
    format_ask, loads_without_tracing, parse_op_plan, OpPlan, MAX_ASK_OPTIONS,
};

use common::plan_of;
// ── §11 interactive replies (`ask`) ──
// This server is a TOOL, not a chat: the card is validated exactly like every other client,
// then surfaced to the CALLING agent as text + structured data. Previews are never shown.

fn ask_plan(ask: &str) -> OpPlan {
    parse_op_plan(&format!(r#"{{"version":1,"reply":"pick","ask":{ask}}}"#)).unwrap()
}

fn ask_rejects(ask: &str) {
    let text = format!(r#"{{"version":1,"reply":"pick","ask":{ask}}}"#);
    assert!(
        parse_op_plan(&text).is_err(),
        "expected this card to reject the plan: {ask}"
    );
}

#[test]
fn ask_card_parses_with_its_defaults() {
    let plan = ask_plan(r#"{"question":"Which tint?","options":[{"label":"Sepia"},{"label":"B&W"}]}"#);
    let card = plan.ask.expect("card");
    assert_eq!(card.question, "Which tint?");
    assert!(!card.multi);
    assert!(!card.allow_custom);
    assert_eq!(
        card.options.iter().map(|o| o.label.as_str()).collect::<Vec<_>>(),
        vec!["Sepia", "B&W"]
    );
}

#[test]
fn ask_multi_custom_row_and_trimming() {
    let plan = ask_plan(
        r#"{"question":"  Which?  ","mode":"multi","allowCustom":true,"customLabel":" Other ","options":[{"label":"  A  "},{"label":"B"}]}"#,
    );
    let card = plan.ask.expect("card");
    assert!(card.multi);
    assert!(card.allow_custom);
    assert_eq!(card.question, "Which?");
    assert_eq!(card.custom_label, "Other");
    assert_eq!(card.options[0].label, "A");
}

#[test]
fn no_card_on_an_ordinary_or_chat_only_turn() {
    assert!(plan_of(r#"{"version":1,"reply":"hi","actions":[]}"#).ask.is_none());
    assert!(plan_of("just chatting").ask.is_none());
}

#[test]
fn previews_are_dropped_with_one_note_never_the_option() {
    let plan = ask_plan(
        r#"{"question":"Which?","options":[{"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]},{"label":"Web","image":{"url":"https://e/x.png"}}]}"#,
    );
    let card = plan.ask.expect("card");
    assert_eq!(card.options.len(), 2, "both options survive");
    let notes = plan
        .warnings
        .iter()
        .filter(|w| w.contains("option previews"))
        .count();
    assert_eq!(notes, 1, "one note per CARD, not per option");
}

#[test]
fn ask_option_action_previews_and_all_image_reference_forms_are_accepted() {
    // §11.1: options may carry `actions` (a render spec) or `image` — one of
    // url / projectId / scanIndex. This server shows no pictures, so every form
    // is accepted and only the label survives (§11.4) — never a rejection.
    let plan = ask_plan(
        r#"{"question":"Which?","options":[
            {"label":"Crop","actions":[{"op":"crop","spec":{"x1":"10%"}}]},
            {"label":"Web","image":{"url":"https://e/x.png"}},
            {"label":"Draft","image":{"projectId":"p_12"}},
            {"label":"Scan","image":{"scanIndex":3}}
        ]}"#,
    );
    let card = plan.ask.expect("card");
    assert_eq!(
        card.options.iter().map(|o| o.label.as_str()).collect::<Vec<_>>(),
        ["Crop", "Web", "Draft", "Scan"]
    );
}

#[test]
fn malformed_cards_reject_the_whole_plan() {
    ask_rejects(r#""hello""#);
    ask_rejects(r#"{"options":[{"label":"A"},{"label":"B"}]}"#);
    ask_rejects(r#"{"question":"   ","options":[{"label":"A"},{"label":"B"}]}"#);
    ask_rejects(r#"{"question":"Q","options":[]}"#);
    ask_rejects(r#"{"question":"Q","options":[{"label":"only"}]}"#);
    ask_rejects(
        r#"{"question":"Q","options":[{"label":"1"},{"label":"2"},{"label":"3"},{"label":"4"},{"label":"5"},{"label":"6"}]}"#,
    );
    ask_rejects(r#"{"question":"Q","mode":"maybe","options":[{"label":"A"},{"label":"B"}]}"#);
    ask_rejects(r#"{"question":"Q","options":[{"label":"A"},{"label":"B"}],"sneaky":1}"#);
    ask_rejects(r#"{"question":"Q","options":[{"label":"A","sneaky":1},{"label":"B"}]}"#);
    ask_rejects(r#"{"question":"Q","options":[{"label":""},{"label":"B"}]}"#);
    ask_rejects(
        r#"{"question":"Q","options":[{"label":"A","actions":[],"image":{"url":"https://e/x"}},{"label":"B"}]}"#,
    );
    ask_rejects(r#"{"question":"Q","options":[{"label":"A"},{"label":"B"}],"allowCustom":"yes"}"#);
}

#[test]
fn five_options_is_the_cap_and_it_parses() {
    let plan = ask_plan(
        r#"{"question":"Q","options":[{"label":"1"},{"label":"2"},{"label":"3"},{"label":"4"},{"label":"5"}]}"#,
    );
    assert_eq!(plan.ask.expect("card").options.len(), MAX_ASK_OPTIONS);
}

#[test]
fn a_card_is_formatted_as_a_numbered_list_for_the_calling_agent() {
    let plan = ask_plan(
        r#"{"question":"Which tint?","allowCustom":true,"options":[{"label":"Sepia"},{"label":"B&W"}]}"#,
    );
    let text = format_ask(&plan.ask.expect("card"));
    assert!(text.contains("Which tint?"), "got: {text}");
    assert!(text.contains("1. Sepia"), "got: {text}");
    assert!(text.contains("2. B&W"), "got: {text}");
    assert!(text.contains("answer freely"), "got: {text}");
    assert!(text.contains("next prompt"), "the agent is told how to answer: {text}");
}

#[test]
fn a_plan_may_both_edit_and_ask() {
    let plan = parse_op_plan(
        r#"{"version":1,"reply":"cropped; now pick","actions":[{"op":"rotate","dir":"left"}],"ask":{"question":"Which tint?","options":[{"label":"Sepia"},{"label":"B&W"}]}}"#,
    )
    .unwrap();
    assert_eq!(plan.actions.len(), 1);
    assert_eq!(plan.ask.expect("card").options.len(), 2);
}

// ── §7 auto-continuation ──

/// §7 auto-continuation: a plan whose actions load a picture (`blank`/`frame`) and drew
/// no layout LINE continues; drawn lines, variants, or an `ask` finish the turn.
#[test]
fn loads_without_tracing_follows_the_section7_rule() {
    let plan = |json: &str| parse_op_plan(json).unwrap();

    // A bare load continues; so does load + pixel-independent edits (crop/filter/page).
    assert!(loads_without_tracing(&plan(
        r##"{"reply":"x","actions":[{"op":"blank","color":"#ffffff"}]}"##
    )));
    assert!(loads_without_tracing(&plan(
        r##"{"reply":"x","actions":[{"op":"frame","index":0},{"op":"filter","mode":"bw"}]}"##
    )));

    // A drawn layout committed to its coordinates — but an EMPTY layout drew nothing.
    assert!(!loads_without_tracing(&plan(
        r##"{"reply":"x","actions":[{"op":"blank","color":"#ffffff"},
            {"op":"layout","lines":[{"points":[{"x":1,"y":2}]}]}]}"##
    )));
    assert!(loads_without_tracing(&plan(
        r##"{"reply":"x","actions":[{"op":"blank","color":"#ffffff"},
            {"op":"layout","lines":[]}]}"##
    )));

    // No load op ⇒ nothing new to look at; variants and an ask end the turn.
    assert!(!loads_without_tracing(&plan(
        r##"{"reply":"x","actions":[{"op":"filter","mode":"sepia"}]}"##
    )));
    assert!(!loads_without_tracing(&plan(
        r##"{"reply":"x","actions":[{"op":"blank","color":"#ffffff"}],
            "variants":[{"label":"v","actions":[{"op":"filter","mode":"bw"}]}]}"##
    )));
    assert!(!loads_without_tracing(&plan(
        r##"{"reply":"x","actions":[{"op":"blank","color":"#ffffff"}],
            "ask":{"question":"Which color?","options":[{"label":"Red"},{"label":"Blue"}]}}"##
    )));
}
