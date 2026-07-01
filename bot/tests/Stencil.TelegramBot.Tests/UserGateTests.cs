using Stencil.TelegramBot.Bot.Telegram;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The per-user serialization gate: same-user work runs one at a time, different users run
/// concurrently, and disposing the handle (once, or redundantly) releases exactly one permit.
/// </summary>
public sealed class UserGateTests
{
    [Fact]
    public async Task SameUserRunsOneAtATime()
    {
        UserGate gate = new();
        int active = 0;
        int maxObserved = 0;

        async Task Work()
        {
            using IDisposable handle = await gate.AcquireAsync(1);
            int now = Interlocked.Increment(ref active);
            maxObserved = Math.Max(maxObserved, now);
            await Task.Delay(15);
            Interlocked.Decrement(ref active);
        }

        await Task.WhenAll(Enumerable.Range(0, 8).Select(_ => Work()));

        Assert.Equal(1, maxObserved);
    }

    [Fact]
    public async Task DifferentUsersDoNotBlockEachOther()
    {
        UserGate gate = new();
        using IDisposable heldByUserOne = await gate.AcquireAsync(1);

        // A different user's gate is independent, so this must not wait on user 1's held gate.
        IDisposable heldByUserTwo = await gate.AcquireAsync(2).WaitAsync(TimeSpan.FromSeconds(1));
        heldByUserTwo.Dispose();
    }

    [Fact]
    public async Task SameUserSecondAcquireWaitsForRelease()
    {
        UserGate gate = new();
        IDisposable first = await gate.AcquireAsync(1);

        Task<IDisposable> second = gate.AcquireAsync(1);
        Assert.False(second.IsCompleted);

        first.Dispose();
        IDisposable handle = await second.WaitAsync(TimeSpan.FromSeconds(1));
        handle.Dispose();
    }

    [Fact]
    public async Task DoubleDisposeReleasesOnlyOnePermit()
    {
        UserGate gate = new();
        IDisposable handle = await gate.AcquireAsync(1);
        handle.Dispose();
        handle.Dispose(); // must not add a phantom permit

        IDisposable again = await gate.AcquireAsync(1).WaitAsync(TimeSpan.FromSeconds(1));
        // Permit count is 1, not 2: a concurrent acquire must still block until `again` releases.
        Task<IDisposable> blocked = gate.AcquireAsync(1);
        Assert.False(blocked.IsCompleted);

        again.Dispose();
        IDisposable unblocked = await blocked.WaitAsync(TimeSpan.FromSeconds(1));
        unblocked.Dispose();
    }
}
