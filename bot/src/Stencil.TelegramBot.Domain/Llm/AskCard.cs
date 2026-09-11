namespace Stencil.TelegramBot.Domain.Llm;

// One choice on an AskCard (§11). Label ONLY: no actions preview, and no image reference —
// resolving one would have Telegram fetch a host the model named. Both warn instead (§11.2).
public sealed record AskOption(string Label);

// A §11 question, rendered as an inline keyboard: one button per option, tap-to-toggle plus a
// Send button when Multi. The answer rides the user's next turn; nothing is applied on tap.
public sealed record AskCard(
    string Question,
    bool Multi,
    bool AllowCustom,
    string CustomLabel,
    IReadOnlyList<AskOption> Options);
