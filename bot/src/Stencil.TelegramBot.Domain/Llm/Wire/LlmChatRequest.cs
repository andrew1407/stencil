namespace Stencil.TelegramBot.Domain.Llm.Wire;

// The system prompt plus the FULL replayed history — every provider is stateless (§7).
public sealed record LlmChatRequest
{
    public required string System { get; init; }

    // Oldest-first, the current turn last.
    public required IReadOnlyList<LlmMessage> Messages { get; init; }

    public string? ServerUrl { get; init; }

    public string? ServerToken { get; init; }

    // The profile the user picked with /chatapi; null = the operator's own STENCIL_LLM_* config.
    public LlmOptions? Options { get; init; }
}
