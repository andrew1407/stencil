namespace Stencil.TelegramBot.Domain.Llm;

// How an LLM call failed. Drives the chat wording; none of these yields a plan.
public enum LlmFailure
{
    // Transport/config/provider: unreachable endpoint, non-2xx, bad payload.
    Error,

    // Stopped on max_tokens; truncated text is never parsed as a plan.
    Truncated,

    // The refusal stop reason.
    Refusal,

    // The server's 503 llmDisabled: no key configured, so a configure hint, not a broken call.
    Disabled,
}

// An LLM call that produced no usable reply. The message is surfaced verbatim in chat; Failure
// keeps the contract's max_tokens/refusal stop reasons apart from plain errors (§6.3).
public sealed class LlmException : Exception
{
    public LlmFailure Failure { get; }

    public LlmException(string message, LlmFailure failure = LlmFailure.Error)
        : base(message)
    {
        Failure = failure;
    }

    // Endpoint URLs, env vars, transport errors: the operator's log only, never the chat reply.
    public string? OperatorDetail { get; private init; }

    public static LlmException Deployment(string message, string operatorDetail) =>
        new(message) { OperatorDetail = operatorDetail };
}
