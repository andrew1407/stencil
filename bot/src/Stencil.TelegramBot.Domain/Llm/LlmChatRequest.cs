namespace Stencil.TelegramBot.Domain.Llm;

/// <summary>
/// One chat call: the system prompt plus the full replayed message history (all providers are
/// stateless — <c>llm-contract.md</c> §7). For the <c>stencil-server</c> provider the
/// caller also resolves which server proxies the call and with which bearer token
/// (<see cref="ServerUrl"/>/<see cref="ServerToken"/>), so the Infrastructure adapter stays
/// free of session logic.
/// </summary>
public sealed record LlmChatRequest
{
    public required string System { get; init; }

    /// <summary>Oldest-first user/assistant messages, the current turn last.</summary>
    public required IReadOnlyList<LlmMessage> Messages { get; init; }

    /// <summary>Resolved collaboration-server origin (stencil-server provider only).</summary>
    public string? ServerUrl { get; init; }

    /// <summary>The user's existing bearer token for <see cref="ServerUrl"/> ("" when none).</summary>
    public string? ServerToken { get; init; }

    /// <summary>
    /// Which provider config this call runs against — the profile the user picked with
    /// <c>/chatapi</c>. Null = the operator's own <c>STENCIL_LLM_*</c> configuration, which is
    /// every call until someone picks something else. Resolved by the caller for the same
    /// reason <see cref="ServerUrl"/> is: session state stays out of the Infrastructure adapter.
    /// </summary>
    public LlmOptions? Options { get; init; }
}
