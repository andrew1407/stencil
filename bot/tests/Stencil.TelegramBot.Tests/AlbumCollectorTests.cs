using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Bot.Telegram.Intake;

namespace Stencil.TelegramBot.Tests;

/// <summary><see cref="AlbumCollector"/>'s settle-window timing on its own (<see cref="AlbumTests"/> covers the router path), driven by a test-held gate rather than a sleep.</summary>
public sealed class AlbumCollectorTests
{
    private const long _userId = 7;

    private static AlbumPhoto photo(int id) => new(id, $"file-{id}", null);

    /// <summary>Settle windows the test opens by hand: each wait parks until it is released.</summary>
    private sealed class SettleGate
    {
        private readonly object _lock = new();
        private readonly List<TaskCompletionSource> _pending = new();

        public Task Wait(CancellationToken ct)
        {
            TaskCompletionSource tcs = new(TaskCreationOptions.RunContinuationsAsynchronously);
            lock (_lock)
            {
                _pending.Add(tcs);
            }
            return tcs.Task.WaitAsync(ct);
        }

        /// <summary>Wait until that many windows are open, then let all of them elapse.</summary>
        public async Task ReleaseAsync(int count = 1)
        {
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (true)
            {
                TaskCompletionSource[] ready = [];
                lock (_lock)
                {
                    if (_pending.Count >= count)
                    {
                        ready = [.. _pending];
                        _pending.Clear();
                    }
                }
                if (ready.Length > 0)
                {
                    foreach (TaskCompletionSource tcs in ready)
                    {
                        tcs.SetResult();
                    }
                    return;
                }
                Assert.True(DateTime.UtcNow < deadline, $"fewer than {count} settle window(s) were entered");
                await Task.Delay(1);
            }
        }
    }

    [Fact]
    public async Task Should_Flush_A_Single_Member_Once_The_Window_Elapses()
    {
        SettleGate gate = new();
        AlbumCollector collector = new(gate.Wait);
        List<IReadOnlyList<AlbumPhoto>> flushed = new();

        collector.Add(_userId, "g1", photo(1), photos => { flushed.Add(photos); return Task.CompletedTask; },
            CancellationToken.None);
        Assert.Empty(flushed); // still buffering

        await gate.ReleaseAsync();
        await collector.WhenIdleAsync();

        AlbumPhoto only = Assert.Single(Assert.Single(flushed));
        Assert.Equal(1, only.MessageId);
    }

    [Fact]
    public async Task Should_Restart_The_Window_And_Join_The_Same_Flush_For_A_Late_Member()
    {
        SettleGate gate = new();
        AlbumCollector collector = new(gate.Wait);
        List<IReadOnlyList<AlbumPhoto>> flushed = new();
        Task Flush(IReadOnlyList<AlbumPhoto> photos) { flushed.Add(photos); return Task.CompletedTask; }

        collector.Add(_userId, "g1", photo(1), Flush, CancellationToken.None);
        collector.Add(_userId, "g1", photo(2), Flush, CancellationToken.None); // arrives before the window ends

        // The first window ends with the group having grown, so the collector waits again…
        await gate.ReleaseAsync();
        Assert.Empty(flushed);
        // …and only the quiet window flushes — once, with both members in arrival order.
        await gate.ReleaseAsync();
        await collector.WhenIdleAsync();

        IReadOnlyList<AlbumPhoto> group = Assert.Single(flushed);
        Assert.Equal(new[] { 1, 2 }, group.Select(p => p.MessageId));
    }

    [Fact]
    public async Task Should_Settle_And_Flush_Two_Groups_Independently()
    {
        SettleGate gate = new();
        AlbumCollector collector = new(gate.Wait);
        List<string> flushedGroups = new();

        collector.Add(_userId, "g1", photo(1), _ => { flushedGroups.Add("g1"); return Task.CompletedTask; },
            CancellationToken.None);
        collector.Add(_userId, "g2", photo(2), _ => { flushedGroups.Add("g2"); return Task.CompletedTask; },
            CancellationToken.None);

        await gate.ReleaseAsync(2);
        await collector.WhenIdleAsync();

        Assert.Equal(2, flushedGroups.Count);
        Assert.Contains("g1", flushedGroups);
        Assert.Contains("g2", flushedGroups);
    }

    [Fact]
    public async Task Should_Start_A_Fresh_Group_For_The_Same_Group_Id_After_Its_Flush()
    {
        SettleGate gate = new();
        AlbumCollector collector = new(gate.Wait);
        List<IReadOnlyList<AlbumPhoto>> flushed = new();
        Task Flush(IReadOnlyList<AlbumPhoto> photos) { flushed.Add(photos); return Task.CompletedTask; }

        collector.Add(_userId, "g1", photo(1), Flush, CancellationToken.None);
        await gate.ReleaseAsync();
        await collector.WhenIdleAsync();

        collector.Add(_userId, "g1", photo(2), Flush, CancellationToken.None);
        await gate.ReleaseAsync();
        await collector.WhenIdleAsync();

        Assert.Equal(2, flushed.Count);
        Assert.Equal(1, Assert.Single(flushed[0]).MessageId);
        Assert.Equal(2, Assert.Single(flushed[1]).MessageId); // not re-flushed with the first
    }

    [Fact]
    public async Task Should_Drop_The_Group_Without_Flushing_On_Shutdown_During_The_Window()
    {
        SettleGate gate = new();
        AlbumCollector collector = new(gate.Wait);
        using CancellationTokenSource cts = new();
        bool flushed = false;

        collector.Add(_userId, "g1", photo(1), _ => { flushed = true; return Task.CompletedTask; }, cts.Token);
        await cts.CancelAsync();
        await collector.WhenIdleAsync(); // the started task unwinds, it does not hang

        Assert.False(flushed);
    }
}
