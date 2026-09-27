using Stencil.TelegramBot.Bot;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests.Telegram;

/// <summary>The update pump: queuing never waits on the handler, one user's lane runs in order on one worker at a time so a busy user never starves another, a Stop tap jumps every lane, a flooding lane is capped, a throwing handler is logged, and disposing drains.</summary>
public sealed class UpdatePumpTests
{
    private readonly MockLogger<UpdatePumpTests> _logger = new();

    private UpdatePump make(int workers = 32, int perUser = 64) =>
        new(_logger, new BotOptions { UpdateWorkers = workers, MaxPendingPerUser = perUser });

    private static TaskCompletionSource signal() => new(TaskCreationOptions.RunContinuationsAsynchronously);

    private static Func<Task> blockUntil(TaskCompletionSource started, TaskCompletionSource release) =>
        async () => { started.TrySetResult(); await release.Task; };

    [Fact]
    public async Task Should_Not_Wait_For_The_Handler_When_Queuing()
    {
        await using UpdatePump pump = make();
        TaskCompletionSource release = signal();
        TaskCompletionSource started = signal();

        await pump.EnqueueAsync(1, blockUntil(started, release));
        await started.Task.WaitAsync(TimeSpan.FromSeconds(10));
        TaskCompletionSource second = signal();
        await pump.EnqueueAsync(2, () => { second.SetResult(); return Task.CompletedTask; });

        await second.Task.WaitAsync(TimeSpan.FromSeconds(10));
        release.SetResult();
    }

    [Fact]
    public async Task Should_Serve_Another_User_While_One_User_Has_More_Queued_Than_There_Are_Workers()
    {
        await using UpdatePump pump = make(workers: 2);
        TaskCompletionSource release = signal();
        int busyRan = 0;
        for (int i = 0; i < 40; i++)
        {
            await pump.EnqueueAsync(1, async () => { Interlocked.Increment(ref busyRan); await release.Task; });
        }
        TaskCompletionSource other = signal();

        await pump.EnqueueAsync(2, () => { other.SetResult(); return Task.CompletedTask; });

        await other.Task.WaitAsync(TimeSpan.FromSeconds(10));
        // The busy user holds at most ONE worker; the rest of their updates wait in their own lane.
        Assert.True(Volatile.Read(ref busyRan) <= 1);
        release.SetResult();
    }

    [Fact]
    public async Task Should_Run_A_Laneless_Stop_While_Its_Users_Lane_Is_Busy()
    {
        await using UpdatePump pump = make(workers: 1);
        TaskCompletionSource release = signal();
        TaskCompletionSource started = signal();
        await pump.EnqueueAsync(7, blockUntil(started, release));
        await pump.EnqueueAsync(7, () => Task.CompletedTask);
        await started.Task.WaitAsync(TimeSpan.FromSeconds(10));
        TaskCompletionSource stopped = signal();

        await pump.EnqueueAsync(null, () => { stopped.SetResult(); return Task.CompletedTask; });

        await stopped.Task.WaitAsync(TimeSpan.FromSeconds(10)); // the only worker is still held
        release.SetResult();
    }

    [Fact]
    public async Task Should_Run_One_Users_Updates_In_Arrival_Order()
    {
        List<int> order = [];
        UpdatePump pump = make(workers: 8);
        for (int i = 0; i < 50; i++)
        {
            int n = i;
            await pump.EnqueueAsync(3, async () => { await Task.Yield(); lock (order) { order.Add(n); } });
        }

        await pump.DisposeAsync();

        Assert.Equal(Enumerable.Range(0, 50), order);
    }

    [Fact]
    public async Task Should_Drop_Past_The_Per_User_Cap_And_Keep_Serving_Others()
    {
        await using UpdatePump pump = make(workers: 1, perUser: 2);
        TaskCompletionSource release = signal();
        TaskCompletionSource started = signal();
        await pump.EnqueueAsync(1, blockUntil(started, release));
        await started.Task.WaitAsync(TimeSpan.FromSeconds(10));

        Assert.True(await pump.EnqueueAsync(1, () => Task.CompletedTask));
        Assert.True(await pump.EnqueueAsync(1, () => Task.CompletedTask));
        Assert.False(await pump.EnqueueAsync(1, () => Task.CompletedTask));
        Assert.True(await pump.EnqueueAsync(2, () => Task.CompletedTask));
        Assert.True(await pump.EnqueueAsync(null, () => Task.CompletedTask)); // Stop is never capped
        release.SetResult();
    }

    [Fact]
    public async Task Should_Log_A_Throwing_Handler_And_Keep_The_Worker_Going()
    {
        TaskCompletionSource ran = signal();
        await using (UpdatePump pump = make(workers: 1))
        {
            await pump.EnqueueAsync(1, () => throw new InvalidOperationException("boom"));
            await pump.EnqueueAsync(1, () => { ran.SetResult(); return Task.CompletedTask; });
            await ran.Task.WaitAsync(TimeSpan.FromSeconds(10));
        }

        Assert.Contains("Unhandled error handling an update", _logger.Messages);
    }

    [Fact]
    public async Task Should_Drain_What_Is_Still_Queued_On_Dispose()
    {
        int ran = 0;
        UpdatePump pump = make(workers: 1);
        for (int i = 0; i < 20; i++)
        {
            await pump.EnqueueAsync(i % 3, () => { Interlocked.Increment(ref ran); return Task.CompletedTask; });
        }

        await pump.DisposeAsync();

        Assert.Equal(20, ran);
        Assert.False(await pump.EnqueueAsync(1, () => Task.CompletedTask)); // closed
    }
}
