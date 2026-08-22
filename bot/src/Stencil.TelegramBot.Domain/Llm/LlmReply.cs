namespace Stencil.TelegramBot.Domain.Llm;

/// <summary>
/// A completed chat turn: the model's raw text plus the stop reason (<c>end_turn</c> normally;
/// <c>max_tokens</c>/<c>refusal</c> are surfaced as <see cref="LlmException"/>s by the client
/// instead, so a reply that reaches the caller is always parseable).
/// </summary>
public sealed record LlmReply(string Text, string StopReason = "end_turn");
