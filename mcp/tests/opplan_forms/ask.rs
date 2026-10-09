//! Contract §11 interactive replies (`ask`) as this server types and shows them, and the §7
//! auto-continuation rule. The card's validation is core's.


use stencil_mcp::opplan::{format_ask, loads_without_tracing, OpPlan};

use crate::common::plan_of;

/// A plan whose checked card reads `ask` (core fills `mode`, `allowCustom`, `customLabel`).
fn ask_plan(ask: &str) -> OpPlan {
    plan_of(&format!(r#"{{"reply":"pick","ask":{ask}}}"#))
}

const TINTS: &str = r#"[{"label":"Sepia"},{"label":"B&W"}]"#;

#[test]
fn a_single_choice_card_types_its_question_and_labels() {
    let plan = ask_plan(&format!(
        r#"{{"question":"Which tint?","mode":"single","allowCustom":false,"customLabel":"Something else…","options":{TINTS}}}"#
    ));
    let card = plan.ask.expect("card");
    assert_eq!(card.question, "Which tint?");
    assert!(!card.multi);
    assert!(!card.allow_custom);
    assert_eq!(
        card.options.iter().map(|o| o.label.as_str()).collect::<Vec<_>>(),
        vec!["Sepia", "B&W"]
    );
    assert!(plan.warnings.is_empty(), "no preview, no note: {:?}", plan.warnings);
}

#[test]
fn a_multi_card_with_a_custom_row_types_both() {
    let plan = ask_plan(
        r#"{"question":"Which?","mode":"multi","allowCustom":true,"customLabel":"Other","options":[{"label":"A"},{"label":"B"}]}"#,
    );
    let card = plan.ask.expect("card");
    assert!(card.multi && card.allow_custom);
    assert_eq!(card.custom_label, "Other");
}

#[test]
fn no_card_on_an_ordinary_or_chat_only_turn() {
    assert!(plan_of(r#"{"reply":"hi","actions":[],"ask":null}"#).ask.is_none());
    assert!(plan_of(r#"{"status":"chatOnly","reply":"just chatting"}"#).ask.is_none());
}

#[test]
fn previews_are_dropped_with_one_note_never_the_option() {
    let plan = ask_plan(
        r#"{"question":"Which?","mode":"single","options":[{"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]},{"label":"Web","image":{"url":"https://e/x.png"}}]}"#,
    );
    assert_eq!(plan.ask.expect("card").options.len(), 2, "both options survive");
    let notes = plan.warnings.iter().filter(|w| w.contains("option previews")).count();
    assert_eq!(notes, 1, "one note per CARD, not per option");
}

/// Core already removed a misplaced preview; the card still says previews are not shown,
/// and says it before the plan-wide reply note.
#[test]
fn a_dropped_preview_still_earns_the_note_before_the_reply_note() {
    let plan = plan_of(
        r#"{"reply":"Done.","actions":[{"op":"rotate","dir":"left","times":1}],
            "ask":{"question":"Q","mode":"single","options":[{"label":"A"},{"label":"B"}]},
            "warnings":[{"code":"W_PREVIEW_DROPPED","op":"save","index":1,"label":"A","message":"Dropped the preview for ask option 1 (\"A\") — core's words"},
                        {"code":"W_REPLY_OMITTED","message":"The model omitted its reply — the plan still ran"}]}"#,
    );
    assert_eq!(plan.warnings.len(), 3, "{:?}", plan.warnings);
    assert!(plan.warnings[0].starts_with("Dropped the preview for ask option 1"));
    assert!(plan.warnings[1].contains("option previews are not shown"));
    assert_eq!(plan.warnings[2], "The model omitted its reply — the plan still ran");
}

#[test]
fn every_image_reference_form_keeps_only_its_label() {
    let plan = ask_plan(
        r#"{"question":"Which?","mode":"single","options":[
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
fn a_card_is_formatted_as_a_numbered_list_for_the_calling_agent() {
    let plan = ask_plan(&format!(
        r#"{{"question":"Which tint?","mode":"single","allowCustom":true,"customLabel":"Something else…","options":{TINTS}}}"#
    ));
    let text = format_ask(&plan.ask.expect("card"));
    assert!(text.contains("Which tint?"), "got: {text}");
    assert!(text.contains("1. Sepia"), "got: {text}");
    assert!(text.contains("2. B&W"), "got: {text}");
    assert!(text.contains("answer freely: Something else…"), "got: {text}");
    assert!(text.contains("next prompt"), "the agent is told how to answer: {text}");
}

#[test]
fn a_plan_may_both_edit_and_ask() {
    let plan = plan_of(&format!(
        r#"{{"reply":"cropped; now pick","actions":[{{"op":"rotate","dir":"left","times":1}}],"ask":{{"question":"Which tint?","mode":"single","options":{TINTS}}}}}"#
    ));
    assert_eq!(plan.actions.len(), 1);
    assert_eq!(plan.ask.expect("card").options.len(), 2);
}

/// §7 auto-continuation: a plan whose actions load a picture (`blank`/`frame`) and drew
/// no layout LINE continues; drawn lines, variants, or an `ask` finish the turn.
#[test]
fn loads_without_tracing_follows_the_section7_rule() {
    let blank = r##"{"op":"blank","color":"#ffffff"}"##;
    let plan = |actions: &str, rest: &str| plan_of(&format!(r#"{{"reply":"x","actions":[{actions}]{rest}}}"#));

    // A bare load continues; so does load + pixel-independent edits (crop/filter/page).
    assert!(loads_without_tracing(&plan(blank, "")));
    assert!(loads_without_tracing(&plan(r#"{"op":"frame","index":0},{"op":"filter","mode":"bw"}"#, "")));

    // A drawn layout committed to its coordinates — but an EMPTY layout drew nothing.
    let drawn = format!(r#"{blank},{{"op":"layout","lines":[{{"points":[{{"x":1,"y":2}}]}}]}}"#);
    assert!(!loads_without_tracing(&plan(&drawn, "")));
    assert!(loads_without_tracing(&plan(&format!(r#"{blank},{{"op":"layout","lines":[]}}"#), "")));

    // No load op ⇒ nothing new to look at; variants and an ask end the turn.
    assert!(!loads_without_tracing(&plan(r#"{"op":"filter","mode":"sepia"}"#, "")));
    let variant = r#","variants":[{"label":"v","actions":[{"op":"filter","mode":"bw"}]}]"#;
    assert!(!loads_without_tracing(&plan(blank, variant)));
    let ask = r#","ask":{"question":"Which color?","mode":"single","options":[{"label":"Red"},{"label":"Blue"}]}"#;
    assert!(!loads_without_tracing(&plan(blank, ask)));
}
