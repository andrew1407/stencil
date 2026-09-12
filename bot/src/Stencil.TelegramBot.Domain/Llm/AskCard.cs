namespace Stencil.TelegramBot.Domain.Llm;

// Label ONLY (§11.2): no actions preview, no image reference a model could point Telegram at.
public sealed record AskOption(string Label);

// A §11 question as an inline keyboard; the answer rides the user's next turn, nothing applies on
// tap.
public sealed record AskCard(
    string Question,
    bool Multi,
    bool AllowCustom,
    string CustomLabel,
    IReadOnlyList<AskOption> Options);
