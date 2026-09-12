using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The live-sync poller: per registry entry it compares the server's project version with what
/// the session last saw, pulls and re-renders only when a peer moved ahead, and drops stale
/// entries. Driven through <see cref="IHostedService"/> with the server, CLI and Telegram
/// mocked — the first tick runs immediately, so no test waits out the 6-second cadence.
/// </summary>
public sealed class SyncWatcherTests : IDisposable
{
    private const long _userId = 71;
    private const long _chatId = 72;

    private readonly string _dataDir =
        Path.Combine(Path.GetTempPath(), "stencil-bot-sync-" + Guid.NewGuid().ToString("N"));

    private readonly MockStencilCli _cli = new();
    private readonly MockBotClient _bot = new();
    private readonly InMemorySessionStore _store = new();
    private readonly RecordingServerService _servers = new();
    private readonly SyncRegistry _registry = new();
    private readonly MockLogger<SyncWatcher> _logger = new();
    private readonly EditingService _editing;
    private readonly CommandHandlers _handlers;

    public SyncWatcherTests()
    {
        BotOptions options = new() { DataDir = _dataDir, AllowedUsers = AnyUser.Instance };
        _editing = new EditingService(_cli, new UserWorkspace(options), _store);
        _handlers = TestHandlers.Create(options, _store, _cli, _bot, servers: _servers, editing: _editing);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    /// <summary>A synced session on an active project, with a working image to re-render.</summary>
    private async Task seedSyncedUser(long lastSeenVersion)
    {
        await _editing.BlankAsync(_userId, new BlankSpec(null, null, null, null));
        await _store.SaveAsync(await _store.GetAsync(_userId) with
        {
            SyncEnabled = true,
            ActiveServerUrl = "http://localhost:8090",
            ActiveProjectId = "p1",
            ActiveProjectName = "cat",
            ActiveProjectVersion = lastSeenVersion,
        });
        _registry.Enable(_userId, _chatId);
    }

    /// <summary>Run ticks until <paramref name="done"/>, then stop the service.</summary>
    private async Task watchUntil(Func<bool> done)
    {
        SyncWatcher watcher = new(_registry, _servers, _store, _handlers, _bot, new UserGate(), _logger);
        await watcher.StartAsync(CancellationToken.None);
        DateTime deadline = DateTime.UtcNow.AddSeconds(5);
        while (!done() && DateTime.UtcNow < deadline)
        {
            await Task.Delay(1);
        }
        await watcher.StopAsync(CancellationToken.None);
        Assert.True(done(), "the first tick did not finish within its deadline");
    }

    [Fact]
    public async Task Should_Pull_And_Push_The_Refreshed_Result_On_A_Newer_Server_Version()
    {
        await seedSyncedUser(lastSeenVersion: 1);
        _servers.ActiveVersion = 2;

        await watchUntil(() => _bot.Requests.OfType<SendPhotoRequest>().Any());

        Assert.Equal(1, _servers.Pulls);
        Assert.Contains(_bot.Requests.OfType<SendMessageRequest>(),
            m => m.Text.Contains("a peer changed this project"));
        // The re-render goes to the registered chat, and is NOT re-uploaded as our own edit.
        SendPhotoRequest photo = _bot.Requests.OfType<SendPhotoRequest>().First();
        Assert.Equal(_chatId, photo.ChatId);
        Assert.Empty(_servers.Saves);
    }

    [Fact]
    public async Task Should_Pull_Nothing_On_An_Unchanged_Version()
    {
        await seedSyncedUser(lastSeenVersion: 3);
        _servers.ActiveVersion = 3; // the peer's version is ours

        await watchUntil(() => _servers.VersionPolls > 0);

        Assert.Equal(0, _servers.Pulls);
        Assert.Empty(_bot.Requests);
    }

    [Fact]
    public async Task Should_Skip_An_Unreachable_Server_Without_Pulling()
    {
        await seedSyncedUser(lastSeenVersion: 1);
        _servers.ActiveVersion = null; // the poll could not reach the server

        await watchUntil(() => _servers.VersionPolls > 0);

        Assert.Empty(_bot.Requests);

        Assert.Equal(0, _servers.Pulls);
        Assert.Empty(_logger.Entries); // unreachable is routine, not a warning
    }

    [Fact]
    public async Task Should_Drop_The_Registry_Entry_When_Sync_Is_Turned_Off()
    {
        await seedSyncedUser(lastSeenVersion: 1);
        await _store.SaveAsync(await _store.GetAsync(_userId) with { SyncEnabled = false });

        await watchUntil(() => _registry.Entries().Count == 0);

        Assert.Empty(_registry.Entries());
        Assert.Equal(0, _servers.Pulls);
    }

    [Fact]
    public async Task Should_Drop_The_Registry_Entry_Too_For_A_Dropped_Project()
    {
        await seedSyncedUser(lastSeenVersion: 1);
        await _store.SaveAsync(await _store.GetAsync(_userId) with { ActiveProjectId = null });

        await watchUntil(() => _registry.Entries().Count == 0);

        Assert.Equal(0, _servers.Pulls);
    }

    [Fact]
    public async Task Should_Log_A_Failed_Tick_And_Keep_The_Loop_Up()
    {
        await seedSyncedUser(lastSeenVersion: 1);
        _servers.VersionThrows = new InvalidOperationException("server is down");

        await watchUntil(() => _logger.Entries.Count > 0);

        (LogLevel level, string message) = Assert.Single(_logger.Entries);
        Assert.Equal(LogLevel.Warning, level);
        Assert.Equal("Sync poll iteration failed", message);
    }
}
