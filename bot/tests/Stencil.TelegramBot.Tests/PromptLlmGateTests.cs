using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The process-wide LLM gate around <see cref="PromptService.PromptAsync"/>: full ⇒ the turn
/// answers busy immediately (an <see cref="LlmException"/>, the same path provider errors
/// take) without ever reaching the model; a freed slot admits the next turn; 0 = unlimited
/// lets turns run concurrently. All timing is TCS/semaphore-driven — no sleeps.
/// </summary>
public sealed class PromptLlmGateTests : IDisposable
{
    private const long _userId = 7;
    private static readonly TimeSpan _waitBudget = TimeSpan.FromSeconds(10);

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly MockLlmClient _llm = new();
    private readonly InMemorySessionStore _store = new();
    private readonly EditingService _editing;

    public PromptLlmGateTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-llmgate-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir };
        _editing = new EditingService(_cli, new UserWorkspace(options), _store);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private PromptService makeService(int maxConcurrent) => new(
        _llm, _editing, _store, new LlmOptions(), new MockServerClientFactory(),
        gate: new LlmGate(maxConcurrent));

    /// <summary>A held model call: signalled in flight via Entered, finished by Hold.</summary>
    private TaskCompletionSource holdModel()
    {
        TaskCompletionSource hold = new(TaskCreationOptions.RunContinuationsAsynchronously);
        _llm.Hold = hold;
        return hold;
    }

    [Fact]
    public async Task FullGateAnswersBusyImmediatelyWithoutCallingTheModel()
    {
        PromptService service = makeService(maxConcurrent: 1);
        TaskCompletionSource hold = holdModel();
        Task<PromptOutcome> first = service.PromptAsync(_userId, "one", null);
        Assert.True(await _llm.Entered.WaitAsync(_waitBudget)); // the slot is really held

        LlmException busy = await Assert.ThrowsAsync<LlmException>(
            () => service.PromptAsync(_userId + 1, "two", null));

        Assert.Equal(PromptService.BUSY_REPLY, busy.Message);
        Assert.Equal(LlmFailure.Error, busy.Failure); // not a refusal ⇒ the caller offers Retry
        Assert.Single(_llm.Requests);                 // the busy turn never reached the model
        hold.SetResult();
        Assert.Equal("ok", (await first).Reply);
    }

    [Fact]
    public async Task CompletedTurnFreesTheSlotForTheNextPrompt()
    {
        PromptService service = makeService(maxConcurrent: 1);
        TaskCompletionSource hold = holdModel();
        Task<PromptOutcome> first = service.PromptAsync(_userId, "one", null);
        Assert.True(await _llm.Entered.WaitAsync(_waitBudget));
        hold.SetResult();
        await first;
        _llm.Hold = null;

        PromptOutcome next = await service.PromptAsync(_userId + 1, "two", null);

        Assert.Equal("ok", next.Reply);
        Assert.Equal(2, _llm.Requests.Count);
    }

    [Fact]
    public async Task ZeroMeansUnlimitedAndTurnsRunConcurrently()
    {
        PromptService service = makeService(maxConcurrent: 0);
        TaskCompletionSource hold = holdModel();
        Task<PromptOutcome> first = service.PromptAsync(_userId, "one", null);
        Task<PromptOutcome> second = service.PromptAsync(_userId + 1, "two", null);
        // Both calls reach the model while neither has answered — nothing was gated out.
        Assert.True(await _llm.Entered.WaitAsync(_waitBudget));
        Assert.True(await _llm.Entered.WaitAsync(_waitBudget));
        Assert.Equal(2, _llm.Requests.Count);

        hold.SetResult();
        PromptOutcome[] outcomes = await Task.WhenAll(first, second);
        Assert.All(outcomes, o => Assert.Equal("ok", o.Reply));
    }
}
