using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Bot.Telegram.Access;

namespace Stencil.TelegramBot.Tests.Telegram.Access;

/// <summary>The per-user serialization gate: same-user work runs one at a time, different users concurrently, disposing the handle (once or redundantly) releases exactly one permit, and a user's semaphore is forgotten once nobody holds it.</summary>
public sealed class UserGateTests
{
    [Fact]
    public async Task Should_Run_The_Same_User_One_At_A_Time()
    {
        UserGate gate = new();
        int active = 0;
        int maxObserved = 0;

        async Task work()
        {
            using IDisposable handle = await gate.AcquireAsync(1);
            int now = Interlocked.Increment(ref active);
            maxObserved = Math.Max(maxObserved, now);
            await Task.Delay(15);
            Interlocked.Decrement(ref active);
        }

        await Task.WhenAll(Enumerable.Range(0, 8).Select(_ => work()));

        Assert.Equal(1, maxObserved);
    }

    [Fact]
    public async Task Should_Not_Block_Different_Users_On_Each_Other()
    {
        UserGate gate = new();
        using IDisposable heldByUserOne = await gate.AcquireAsync(1);

        // A different user's gate is independent, so this must not wait on user 1's held gate.
        IDisposable heldByUserTwo = await gate.AcquireAsync(2).WaitAsync(TimeSpan.FromSeconds(1));
        heldByUserTwo.Dispose();
    }

    [Fact]
    public async Task Should_Wait_For_Release_On_A_Second_Acquire_By_The_Same_User()
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
    public async Task Should_Release_Only_One_Permit_On_Double_Dispose()
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

    [Fact]
    public async Task Should_Forget_The_Semaphore_When_The_Last_Holder_Releases()
    {
        UserGate gate = new();
        for (long user = 1; user <= 50; user++)
        {
            using IDisposable handle = await gate.AcquireAsync(user);
            Assert.Equal(1, gate.TrackedUsers);
        }

        Assert.Equal(0, gate.TrackedUsers);
    }

    [Fact]
    public async Task Should_Keep_The_Semaphore_Alive_And_Still_Serialize_With_A_Waiter()
    {
        UserGate gate = new();
        IDisposable first = await gate.AcquireAsync(1);
        Task<IDisposable> second = gate.AcquireAsync(1);

        first.Dispose();   // one waiter left, so the gate must NOT be evicted under it
        IDisposable handle = await second.WaitAsync(TimeSpan.FromSeconds(1));
        Assert.Equal(1, gate.TrackedUsers);

        handle.Dispose();
        Assert.Equal(0, gate.TrackedUsers);
    }

    [Fact]
    public async Task Should_Not_Leak_A_Gate_On_A_Cancelled_Wait()
    {
        UserGate gate = new();
        using IDisposable held = await gate.AcquireAsync(1);
        using CancellationTokenSource cts = new();
        Task<IDisposable> queued = gate.AcquireAsync(1, cts.Token);

        await cts.CancelAsync();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => queued);

        Assert.Equal(1, gate.TrackedUsers);   // still held by the caller above
    }
}
