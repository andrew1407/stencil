namespace Stencil.TelegramBot.Domain.Llm;

// Process-wide cap on concurrent LLM calls (the server's LLM_MAX_IN_FLIGHT model): a call in
// flight can run for minutes holding image payloads, so N users prompting must not mean N
// upstream calls. Non-blocking on purpose — queueing behind a full gate answers late anyway.
public sealed class LlmGate
{
    private readonly SemaphoreSlim? _slots;

    // 0 or less = unlimited.
    public LlmGate(int maxConcurrent) =>
        _slots = maxConcurrent <= 0 ? null : new SemaphoreSlim(maxConcurrent, maxConcurrent);

    // False when the gate is full.
    public bool TryEnter() => _slots is null || _slots.Wait(0);

    public void Exit() => _slots?.Release();
}
