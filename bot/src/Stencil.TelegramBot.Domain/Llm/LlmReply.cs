namespace Stencil.TelegramBot.Domain.Llm;

// max_tokens/refusal are surfaced as LlmExceptions by the client instead, so a reply that
// reaches the caller is always parseable.
public sealed record LlmReply(string Text, string StopReason = "end_turn");
