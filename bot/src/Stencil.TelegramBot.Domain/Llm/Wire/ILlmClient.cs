namespace Stencil.TelegramBot.Domain.Llm.Wire;

// One non-streaming turn mapped onto the provider's wire format (§6); plan parsing is
// Application's.
public interface ILlmClient
{
    // Throws LlmException; Truncated/Refusal stops are never parsed as plans.
    Task<LlmReply> ChatAsync(LlmChatRequest request, CancellationToken ct = default);
}
