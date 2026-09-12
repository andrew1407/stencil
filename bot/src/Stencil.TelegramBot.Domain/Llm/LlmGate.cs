namespace Stencil.TelegramBot.Domain.Llm;

// Process-wide cap on concurrent LLM calls (the server's LLM_MAX_IN_FLIGHT model). Non-blocking on
// purpose: queueing behind a full gate answers late anyway.
public sealed class LlmGate
{
    private readonly SemaphoreSlim? _slots;

    // 0 or less = unlimited.
    public LlmGate(int maxConcurrent) =>
        _slots = maxConcurrent <= 0 ? null : new SemaphoreSlim(maxConcurrent, maxConcurrent);

    public bool TryEnter() => _slots is null || _slots.Wait(0);

    public void Exit() => _slots?.Release();
}
