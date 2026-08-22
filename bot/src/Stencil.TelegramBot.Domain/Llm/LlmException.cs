namespace Stencil.TelegramBot.Domain.Llm;

/// <summary>How an LLM call failed (drives the chat wording; none of these yield a plan).</summary>
public enum LlmFailure
{
    /// <summary>Transport/config/provider error (unreachable endpoint, non-2xx, bad payload).</summary>
    Error,

    /// <summary>The reply stopped on <c>max_tokens</c> — truncated text is never parsed as a plan.</summary>
    Truncated,

    /// <summary>The model refused (<c>refusal</c> stop reason) — shown as a chat error, never a plan.</summary>
    Refusal,

    /// <summary>The server's 503 <c>llmDisabled</c>: no LLM key configured — a configure hint, not a broken call.</summary>
    Disabled,
}

/// <summary>
/// An LLM call that produced no usable reply. The message is human-readable and surfaced
/// verbatim in chat; <see cref="Failure"/> distinguishes the contract's <c>max_tokens</c> and
/// <c>refusal</c> stop reasons from plain errors (<c>llm-contract.md</c> §6.3).
/// </summary>
public sealed class LlmException : Exception
{
    public LlmFailure Failure { get; }

    public LlmException(string message, LlmFailure failure = LlmFailure.Error)
        : base(message)
    {
        Failure = failure;
    }

    /// <summary>
    /// Deployment detail (endpoint URLs, env vars, transport errors) for the operator's log
    /// only — the chat reply is <see cref="Exception.Message"/>. Null when there is none.
    /// </summary>
    public string? OperatorDetail { get; private init; }

    /// <summary>A failure whose real cause is operator-only: plain message out, detail logged.</summary>
    public static LlmException Deployment(string message, string operatorDetail) =>
        new(message) { OperatorDetail = operatorDetail };
}
