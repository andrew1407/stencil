using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Stencil.TelegramBot.Bot.Telegram.Commands;
using Stencil.TelegramBot.Bot.Telegram.Sync;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Bot.Telegram.Access;
using Stencil.TelegramBot.Bot.Telegram.Messaging;
using Stencil.TelegramBot.Tests.Telegram.Access;

namespace Stencil.TelegramBot.Tests.Telegram.Sync;

/// <summary>The live-sync poller: per registry entry it compares the server's project version with what the session last saw, pulls and re-renders only when a peer moved ahead, and drops stale entries. The first tick runs immediately, so no test waits out the <see cref="BotOptions.SyncPollInterval"/> cadence.</summary>
public sealed class SyncWatcherTests : IDisposable
{
    private const long _userId = 71;
    private const long _chatId = 72;

    private readonly string _dataDir =
        TempDirs.New("bot-sync");

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
        TempDirs.Delete(_dataDir);
    }

    /// <summary>A synced session on an active project, with a working image to re-render.</summary>
    private async Task seedSyncedUser(long lastSeenVersion, long userId = _userId, long chatId = _chatId)
    {
        await _editing.BlankAsync(userId, new BlankSpec(null, null, null, null));
        await _store.SaveAsync(await _store.GetAsync(userId) with
        {
            SyncEnabled = true,
            ActiveServerUrl = "http://localhost:8090",
            ActiveProjectId = "p1",
            ActiveProjectName = "cat",
            ActiveProjectVersion = lastSeenVersion,
        });
        _registry.Enable(userId, chatId);
    }

    /// <summary>Run ticks until <paramref name="done"/>, then stop the service.</summary>
    private async Task watchUntil(Func<bool> done, UserGate? gate = null)
    {
        SyncWatcher watcher = new(_registry, _servers, _store, _handlers, _bot, gate ?? new UserGate(), new BotOptions(), _logger);
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
        Assert.Equal("Sync poll failed for user 71", message);
    }

    [Fact]
    public async Task Should_Skip_A_Busy_User_And_Still_Pull_The_Next()
    {
        await seedSyncedUser(lastSeenVersion: 1);
        await seedSyncedUser(lastSeenVersion: 1, userId: 81, chatId: 82);
        _servers.ActiveVersion = 2;
        UserGate gate = new();
        using IDisposable busy = await gate.AcquireAsync(_userId); // a long assistant turn

        await watchUntil(() => _servers.PulledUsers.Contains(81), gate);

        Assert.DoesNotContain(_userId, _servers.PulledUsers);
    }

    [Fact]
    public async Task Should_Keep_Polling_Others_When_One_Users_Pull_Fails()
    {
        await seedSyncedUser(lastSeenVersion: 1);
        await seedSyncedUser(lastSeenVersion: 1, userId: 81, chatId: 82);
        _servers.ActiveVersion = 2;
        _servers.FailingUsers.Add(_userId);

        await watchUntil(() => _servers.PulledUsers.Contains(81) && _logger.Entries.Count > 0);

        Assert.Contains(_logger.Messages, m => m == "Sync poll failed for user 71");
    }

    // The registry is in memory while SyncEnabled persists: after a restart, the next update re-registers.
    [Fact]
    public async Task Should_Re_Register_A_Synced_Chat_On_Its_Next_Update()
    {
        await seedSyncedUser(lastSeenVersion: 1);
        _registry.Disable(_userId); // a restart forgot it
        UpdateRouter router = new(_handlers, new CallbackAction(_handlers, _bot, _store), _editing, _store, _bot,
            new UserGate(), new BotOptions { AllowedUsers = AnyUser.Instance }, new MockLogger<UpdateRouter>(), sync: _registry);

        await router.HandleMessageAsync(AccessAdmissionTests.TextFrom(_userId, "/status"), CancellationToken.None);

        Assert.Contains((_userId, 66L), _registry.Entries());
    }
}
