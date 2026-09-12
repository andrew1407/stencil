using Stencil.TelegramBot.Bot;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The bounded update pump: queuing never waits on the handler (the Stop-tap freeze it exists to
/// prevent), a throwing handler is logged and does not kill its worker, and disposing drains.
/// </summary>
public sealed class UpdatePumpTests
{
    private readonly MockLogger<UpdatePumpTests> _logger = new();

    [Fact]
    public async Task Should_Not_Wait_For_The_Handler_When_Queuing()
    {
        await using UpdatePump pump = new(_logger);
        TaskCompletionSource release = new(TaskCreationOptions.RunContinuationsAsynchronously);
        TaskCompletionSource started = new(TaskCreationOptions.RunContinuationsAsynchronously);

        await pump.EnqueueAsync(async () => { started.SetResult(); await release.Task; });
        await started.Task.WaitAsync(TimeSpan.FromSeconds(10));
        // The long handler is still running, yet a later update runs anyway — ordering across
        // users is not this queue's job.
        TaskCompletionSource second = new(TaskCreationOptions.RunContinuationsAsynchronously);
        await pump.EnqueueAsync(() => { second.SetResult(); return Task.CompletedTask; });

        await second.Task.WaitAsync(TimeSpan.FromSeconds(10));
        release.SetResult();
    }

    [Fact]
    public async Task Should_Log_A_Throwing_Handler_And_Keep_The_Worker_Going()
    {
        TaskCompletionSource ran = new(TaskCreationOptions.RunContinuationsAsynchronously);
        await using (UpdatePump pump = new(_logger, workers: 1))
        {
            await pump.EnqueueAsync(() => throw new InvalidOperationException("boom"));
            await pump.EnqueueAsync(() => { ran.SetResult(); return Task.CompletedTask; });
            await ran.Task.WaitAsync(TimeSpan.FromSeconds(10));
        }

        Assert.Contains("Unhandled error handling an update", _logger.Messages);
    }

    [Fact]
    public async Task Should_Drain_What_Is_Still_Queued_On_Dispose()
    {
        int ran = 0;
        UpdatePump pump = new(_logger, workers: 1);
        for (int i = 0; i < 20; i++)
        {
            await pump.EnqueueAsync(() => { Interlocked.Increment(ref ran); return Task.CompletedTask; });
        }

        await pump.DisposeAsync();

        Assert.Equal(20, ran);
    }
}
