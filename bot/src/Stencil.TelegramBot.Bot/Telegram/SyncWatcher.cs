using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Background poller that gives the REST-only bot a live feel (the analogue of the CLI's
/// <c>/sync</c> auto-pull). Every few seconds it checks each sync-enabled user's active project
/// version on the server; when a peer's change bumps it past what the session last saw, it pulls
/// the new layout+image and pushes the refreshed result into the chat.
/// </summary>
public sealed class SyncWatcher
{
    private static readonly TimeSpan Interval = TimeSpan.FromSeconds(6);

    private readonly SyncRegistry _registry;
    private readonly IServerService _servers;
    private readonly ISessionStore _store;
    private readonly CommandHandlers _handlers;
    private readonly ITelegramBotClient _bot;
    private readonly ILogger<SyncWatcher> _logger;

    public SyncWatcher(
        SyncRegistry registry,
        IServerService servers,
        ISessionStore store,
        CommandHandlers handlers,
        ITelegramBotClient bot,
        ILogger<SyncWatcher> logger)
    {
        _registry = registry;
        _servers = servers;
        _store = store;
        _handlers = handlers;
        _bot = bot;
        _logger = logger;
    }

    /// <summary>Run the poll loop until <paramref name="ct"/> is cancelled.</summary>
    public async Task RunAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                await TickAsync(ct);
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

    /// <summary>One poll pass over every sync-enabled user.</summary>
    private async Task TickAsync(CancellationToken ct)
    {
        foreach (var (userId, chatId) in _registry.Entries())
        {
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
            // A peer advanced the version — pull their layout + image and show it.
            await _servers.PullActiveAsync(userId, ct);
            await _bot.SendMessage(chatId, "↺ a peer changed this project — pulled their version.", cancellationToken: ct);
            await _handlers.RenderAndSendAsync(userId, chatId, ct, mutating: false);
        }
    }
}
