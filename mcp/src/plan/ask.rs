//! §11 interactive replies: the checked `ask` card typed for this server, and rendered as text
//! for the calling agent.

use super::result::AskDoc;
use super::{AskCard, AskOption};

/// This server cannot show a picture, so a card with previews says so once.
pub(super) const PREVIEWS_NOT_SHOWN: &str =
    "note: option previews are not shown by this server — the choices are listed by name";

/// Core's checked card → the typed card, plus whether any option carried a preview this server
/// cannot show (`dropped`: core already removed a misplaced one). Only the labels survive.
pub(super) fn card(ask: AskDoc, dropped: bool) -> (AskCard, bool) {
    let previews = dropped || ask.options.iter().any(|o| o.actions.is_some() || o.image.is_some());
    let options = ask.options.into_iter().map(|o| AskOption { label: o.label }).collect();
    let card = AskCard {
        question: ask.question,
        multi: ask.mode == "multi",
        allow_custom: ask.allow_custom,
        custom_label: ask.custom_label,
        options,
    };
    (card, previews)
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
