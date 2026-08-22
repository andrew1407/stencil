namespace Stencil.TelegramBot.Domain.Llm;

/// <summary>
/// One non-streaming LLM chat turn, provider-agnostic. The Infrastructure adapter maps a
/// request onto the configured provider's wire format (<c>llm-contract.md</c> §6) and
/// returns the raw reply text — plan parsing happens in the Application layer.
/// </summary>
public interface ILlmClient
{
    /// <summary>
    /// Send the system prompt plus the replayed conversation and return the model's raw text.
    /// Throws <see cref="LlmException"/> on transport/provider errors, and with
    /// <see cref="LlmFailure.Truncated"/> / <see cref="LlmFailure.Refusal"/> when the reply
    /// stopped on <c>max_tokens</c> / <c>refusal</c> (such replies are never parsed as plans).
    /// </summary>
    Task<LlmReply> ChatAsync(LlmChatRequest request, CancellationToken ct = default);
}
