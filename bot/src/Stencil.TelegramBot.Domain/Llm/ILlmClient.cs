namespace Stencil.TelegramBot.Domain.Llm;

// One non-streaming chat turn, provider-agnostic: the adapter maps it onto the configured
// provider's wire format (§6) and returns raw text. Plan parsing is the Application layer's.
public interface ILlmClient
{
    // Throws LlmException on transport/provider errors, and with Truncated/Refusal when the
    // reply stopped on max_tokens/refusal — those are never parsed as plans.
    Task<LlmReply> ChatAsync(LlmChatRequest request, CancellationToken ct = default);
}
