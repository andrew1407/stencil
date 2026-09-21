using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Stencil.TelegramBot.Bot.Telegram.Sync;

namespace Stencil.TelegramBot.Tests;

/// <summary>The hosted sweeper around <c>UserWorkspace.PruneStale</c> (whose rules <see cref="UserWorkspacePruneTests"/> pin): it visits every on-disk user, keeps what that user's session references, and survives a store that throws.</summary>
public sealed class WorkspaceJanitorTests : IDisposable
{
    private readonly string _root =
        Path.Combine(Path.GetTempPath(), "stencil-bot-janitor-" + Guid.NewGuid().ToString("N"));

    private readonly BotOptions _options;
    private readonly UserWorkspace _workspace;
    private readonly InMemorySessionStore _store = new();
    private readonly MockLogger<WorkspaceJanitor> _logger = new();

    public WorkspaceJanitorTests()
    {
        _options = new BotOptions { DataDir = _root, WorkspaceTtl = TimeSpan.FromHours(1) };
        _workspace = new UserWorkspace(_options);
    }

    public void Dispose()
    {
        try { Directory.Delete(_root, recursive: true); } catch { /* best effort */ }
    }

    private string stale(long userId)
    {
        string path = _workspace.NewFilePath(userId, ".png");
        File.WriteAllBytes(path, new byte[1]);
        File.SetLastWriteTimeUtc(path, DateTime.UtcNow - TimeSpan.FromHours(5));
        return path;
    }

    /// <summary>Run one sweep: start the service, wait for <paramref name="done"/>, stop it.</summary>
    private async Task sweepAsync(IUserWorkspace workspace, ISessionStore store, Func<bool> done)
    {
        WorkspaceJanitor janitor = new(workspace, store, _options, _logger);
        await janitor.StartAsync(CancellationToken.None);
        DateTime deadline = DateTime.UtcNow.AddSeconds(5);
        while (!done() && DateTime.UtcNow < deadline)
        {
            await Task.Delay(1);
        }
        await janitor.StopAsync(CancellationToken.None);
        Assert.True(done(), "the sweep did not finish within its deadline");
    }

    [Fact]
    public async Task Should_Sweep_Every_User_And_Keep_What_Their_Session_References()
    {
        string keptOrphan = stale(7);
        string sweptOrphan = stale(7);
        string otherUser = stale(8);
        await _store.SaveAsync(await _store.GetAsync(7) with { OriginalImagePath = keptOrphan });

        await sweepAsync(_workspace, _store, () => !File.Exists(sweptOrphan) && !File.Exists(otherUser));

        Assert.True(File.Exists(keptOrphan));   // referenced by the session, however old
        Assert.False(File.Exists(sweptOrphan));
        Assert.False(File.Exists(otherUser));   // every on-disk user is visited, not just one
        Assert.Contains(_logger.Messages, m => m.Contains("removed 2 stale file(s)"));
    }

    [Fact]
    public async Task Should_Keep_A_Video_Source_Together_With_The_Original()
    {
        string original = stale(9);
        string video = stale(9);
        string orphan = stale(9);
        await _store.SaveAsync(await _store.GetAsync(9) with
        {
            OriginalImagePath = original,
            VideoSourcePath = video,
        });

        await sweepAsync(_workspace, _store, () => !File.Exists(orphan));

        Assert.True(File.Exists(original));
        Assert.True(File.Exists(video));
    }

    [Fact]
    public async Task Should_Log_A_Sweep_That_Throws_And_Keep_The_Loop_Up()
    {
        stale(7);
        ThrowingStore store = new();

        await sweepAsync(_workspace, store, () => _logger.Entries.Count > 0);

        (LogLevel level, string message) = Assert.Single(_logger.Entries);
        Assert.Equal(LogLevel.Warning, level);
        Assert.Equal("Workspace sweep failed", message);
    }

    [Fact]
    public async Task Should_Log_Nothing_When_There_Is_Nothing_To_Sweep()
    {
        WorkspaceJanitor janitor = new(_workspace, _store, _options, _logger);

        await janitor.StartAsync(CancellationToken.None);
        await janitor.StopAsync(CancellationToken.None); // stops promptly; it does not wait out the cadence

        Assert.Empty(_logger.Entries);
    }

    /// <summary>A session store whose reads fail — the sweep's one external call.</summary>
    private sealed class ThrowingStore : ISessionStore
    {
        public Task<UserSession> GetAsync(long userId, CancellationToken ct = default) =>
            throw new InvalidOperationException("store is down");

        public Task SaveAsync(UserSession session, CancellationToken ct = default) => Task.CompletedTask;

        public Task ResetAsync(long userId, CancellationToken ct = default) => Task.CompletedTask;
    }
}
