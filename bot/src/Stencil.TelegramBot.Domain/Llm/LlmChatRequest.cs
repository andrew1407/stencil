namespace Stencil.TelegramBot.Domain.Llm;

// The system prompt plus the FULL replayed history — every provider is stateless (§7). The
// caller also resolves ServerUrl/ServerToken, so the adapter stays free of session logic.
public sealed record LlmChatRequest
{
    public required string System { get; init; }

    // Oldest-first, the current turn last.
    public required IReadOnlyList<LlmMessage> Messages { get; init; }

    // stencil-server provider only.
    public string? ServerUrl { get; init; }

    // The user's existing bearer for ServerUrl; "" when none.
    public string? ServerToken { get; init; }

    // The profile the user picked with /chatapi; null = the operator's own STENCIL_LLM_* config.
    public LlmOptions? Options { get; init; }
}
