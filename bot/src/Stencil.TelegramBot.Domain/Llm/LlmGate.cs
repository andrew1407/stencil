namespace Stencil.TelegramBot.Domain.Llm;

/// <summary>
/// Process-wide cap on concurrent LLM calls (the server's <c>LLM_MAX_IN_FLIGHT</c> model):
/// each call in flight can run for minutes holding image payloads, so N users prompting must
/// not mean N upstream calls. Non-blocking on purpose — queueing behind a full gate would
/// hold the user for the whole upstream timeout and answer late anyway.
/// </summary>
public sealed class LlmGate
{
    private readonly SemaphoreSlim? _slots;

    /// <summary>At most <paramref name="maxConcurrent"/> calls at once; 0 (or less) = unlimited.</summary>
    public LlmGate(int maxConcurrent) =>
        _slots = maxConcurrent <= 0 ? null : new SemaphoreSlim(maxConcurrent, maxConcurrent);

    /// <summary>Take a slot without blocking; false when the gate is full.</summary>
    public bool TryEnter() => _slots is null || _slots.Wait(0);

    /// <summary>Return the slot a successful <see cref="TryEnter"/> took.</summary>
    public void Exit() => _slots?.Release();
}
