namespace Stencil.TelegramBot.Domain.Llm;

/// <summary>
/// One choice on an <see cref="AskCard"/> (<c>llm-contract.md</c> §11). Label only: this
/// chat renders no <c>actions</c> preview and resolves no <c>image</c> reference (a model-named
/// URL would have Telegram fetch a host the user never chose). Both warn instead (§11.2).
/// </summary>
public sealed record AskOption(string Label);

/// <summary>
/// A question the assistant puts back to the user (<c>llm-contract.md</c> §11), rendered as
/// an inline keyboard — one button per option, tap-to-toggle plus a Send button when
/// <see cref="Multi"/>. The answer is sent as the user's next turn; nothing is applied on tap.
/// </summary>
public sealed record AskCard(
    string Question,
    bool Multi,
    bool AllowCustom,
    string CustomLabel,
    IReadOnlyList<AskOption> Options);
