using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Variant rendering: every variant is an independent CLI run folded from the same post-actions
/// state, so they run together rather than end to end — and the renders still come back in plan
/// order, whichever finishes first.
/// </summary>
public sealed class PromptVariantsTests(PromptServiceFixture fixture) : PromptServiceTestBase(fixture)
{
    private const string ThreeVariants =
        """
        {"reply":"three takes","actions":[],"variants":[
          {"label":"one","actions":[{"op":"rotate","dir":"left","times":1}]},
          {"label":"two","actions":[{"op":"filter","mode":"sepia"}]},
          {"label":"three","actions":[{"op":"filter","mode":"bw"}]}]}
        """;

    [Fact]
    public async Task EveryVariantRenderIsInFlightAtOnce()
    {
        await _editing.BlankAsync(UserId, new BlankSpec(null, null, null, null));
        using SemaphoreSlim started = new(0);
        TaskCompletionSource release = new(TaskCreationOptions.RunContinuationsAsynchronously);
        _cli.BeforeEdit = async () =>
        {
            started.Release();
            await release.Task;
        };
        _llm.CannedReplies.Enqueue(new LlmReply(ThreeVariants));

        Task<PromptOutcome> turn = _service.PromptAsync(UserId, "three takes", null, CancellationToken.None);
        // Sequential rendering would never reach the second call while the first is held open.
        for (int i = 0; i < 3; i++)
        {
            Assert.True(await started.WaitAsync(TimeSpan.FromSeconds(10)), $"only {i} render(s) started");
        }
        release.SetResult();

        PromptOutcome outcome = await turn;
        Assert.Equal(3, outcome.Renders.Count);
    }

    [Fact]
    public async Task RendersComeBackInPlanOrderWhicheverFinishesFirst()
    {
        await _editing.BlankAsync(UserId, new BlankSpec(null, null, null, null));
        // Reverse the completion order: the first variant's run is the slowest.
        int call = 0;
        _cli.BeforeEdit = () => Task.Delay(TimeSpan.FromMilliseconds(Interlocked.Increment(ref call) switch
        {
            1 => 120,
            2 => 60,
            _ => 0,
        }));
        _llm.CannedReplies.Enqueue(new LlmReply(ThreeVariants));

        PromptOutcome outcome = await _service.PromptAsync(UserId, "three takes", null, CancellationToken.None);

        Assert.Equal(new[] { "one", "two", "three" }, outcome.Renders.Select(r => r.Label));
    }
}
