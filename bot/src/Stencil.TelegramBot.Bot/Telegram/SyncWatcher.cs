using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;

namespace Stencil.TelegramBot.Bot.Telegram;

// The REST-only analogue of the CLI's /sync auto-pull: polls each sync-enabled user's active
// project version and, when a peer bumped it, pulls the new layout+image and pushes the refreshed
// result into the chat.
public sealed class SyncWatcher : BackgroundService
{
    private static readonly TimeSpan Interval = TimeSpan.FromSeconds(6);

    private readonly SyncRegistry _registry;
    private readonly IServerService _servers;
    private readonly ISessionStore _store;
    private readonly CommandHandlers _handlers;
    private readonly ITelegramBotClient _bot;
    private readonly UserGate _gate;
    private readonly ILogger<SyncWatcher> _logger;

    public SyncWatcher(
        SyncRegistry registry,
        IServerService servers,
        ISessionStore store,
        CommandHandlers handlers,
        ITelegramBotClient bot,
        UserGate gate,
        ILogger<SyncWatcher> logger)
    {
        _registry = registry;
        _servers = servers;
        _store = store;
        _handlers = handlers;
        _bot = bot;
        _gate = gate;
        _logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                await tickAsync(ct);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                _logger.LogWarning(ex, "Sync poll iteration failed");
            }
            try
            {
                await Task.Delay(Interval, ct);
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }

    private async Task tickAsync(CancellationToken ct)
    {
        foreach (var (userId, chatId) in _registry.Entries())
        {
            // Hold the user's gate for the whole pull so a background refresh can't interleave with
            // an interactive edit.
            using IDisposable gate = await _gate.AcquireAsync(userId, ct);
            UserSession session = await _store.GetAsync(userId, ct);
            if (!session.SyncEnabled || session.ActiveProjectId is null)
            {
                _registry.Disable(userId); // stale entry — user turned sync off or dropped the project
                continue;
            }
            long? serverVersion = await _servers.ActiveServerVersionAsync(userId, ct);
            if (serverVersion is null || serverVersion.Value <= session.ActiveProjectVersion)
            {
                continue; // unreachable, or no change since our last-seen version
            }
            await _servers.PullActiveAsync(userId, ct);
            await _bot.SendMessage(chatId, "↺ a peer changed this project — pulled their version.", cancellationToken: ct);
            await _handlers.RenderAndSendAsync(userId, chatId, ct, mutating: false);
        }
    }
}
