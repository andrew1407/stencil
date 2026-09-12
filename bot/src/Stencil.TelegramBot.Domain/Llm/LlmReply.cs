namespace Stencil.TelegramBot.Domain.Llm;

// max_tokens/refusal surface as LlmExceptions instead, so a reply that reaches the caller is
// parseable.
public sealed record LlmReply(string Text, string StopReason = "end_turn");
