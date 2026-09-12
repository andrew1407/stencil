namespace Stencil.TelegramBot.Domain.Llm;

public enum LlmFailure
{
    ERROR,

    // Stopped on max_tokens; truncated text is never parsed as a plan.
    TRUNCATED,

    REFUSAL,

    // The server's 503 llmDisabled: no key configured, so a configure hint, not a broken call.
    DISABLED,
}

// The message is surfaced verbatim in chat; Failure keeps §6.3's stop reasons apart from errors.
public sealed class LlmException : Exception
{
    public LlmFailure Failure { get; }

    public LlmException(string message, LlmFailure failure = LlmFailure.ERROR)
        : base(message)
    {
        Failure = failure;
    }

    // Endpoint URLs, env vars, transport errors: the operator's log only, never the chat reply.
    public string? OperatorDetail { get; private init; }

    public static LlmException Deployment(string message, string operatorDetail) =>
        new(message) { OperatorDetail = operatorDetail };
}
