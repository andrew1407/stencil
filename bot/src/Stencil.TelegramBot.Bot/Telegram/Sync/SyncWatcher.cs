using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Configuration;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Stencil.TelegramBot.Bot.Telegram.Access;
using Stencil.TelegramBot.Bot.Telegram.Commands;
using Stencil.TelegramBot.Bot.Telegram.Messaging;

namespace Stencil.TelegramBot.Bot.Telegram.Sync;

// The REST-only analogue of the CLI's /sync auto-pull: polls each sync-enabled user's active project
// version and, when a peer bumped it, pulls the new layout+image into the chat.
public sealed class SyncWatcher : BackgroundService
{
    private readonly SyncRegistry _registry;
    private readonly IServerService _servers;
    private readonly ISessionStore _store;
    private readonly CommandHandlers _handlers;
    private readonly ITelegramBotClient _bot;
    private readonly UserGate _gate;
    private readonly IBotPolicy _options;
    private readonly ILogger<SyncWatcher> _logger;

    public SyncWatcher(
        SyncRegistry registry,
        IServerService servers,
        ISessionStore store,
        CommandHandlers handlers,
        ITelegramBotClient bot,
        UserGate gate,
        IBotPolicy options,
        ILogger<SyncWatcher> logger)
    {
        _registry = registry;
        _servers = servers;
        _store = store;
        _handlers = handlers;
        _bot = bot;
        _gate = gate;
        _options = options;
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
                await Task.Delay(_options.SyncPollInterval, ct);
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }

    // A busy user is skipped until the next tick, and one user's failing pull never stops the rest.
    private async Task tickAsync(CancellationToken ct)
    {
        foreach (var (userId, chatId) in _registry.Entries())
        {
            using IDisposable? gate = _gate.TryAcquire(userId);
            if (gate is null)
            {
                continue;
            }
            try
            {
                await pullAsync(userId, chatId, ct);
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                _logger.LogWarning(ex, "Sync poll failed for user {UserId}", userId);
            }
        }
    }

    // Under the user's gate, so a background refresh can't interleave with an interactive edit.
    private async Task pullAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (!session.SyncEnabled || session.ActiveProjectId is null)
        {
            _registry.Disable(userId); // stale entry — user turned sync off or dropped the project
            return;
        }
        long? serverVersion = await _servers.ActiveServerVersionAsync(userId, ct);
        if (serverVersion is null || serverVersion.Value <= session.ActiveProjectVersion)
        {
            return; // unreachable, or no change since our last-seen version
        }
        await _servers.PullActiveAsync(userId, ct);
        await _bot.SendMessage(chatId, Replies.SyncPulled(), cancellationToken: ct);
        await _handlers.RenderAndSendAsync(userId, chatId, ct, mutating: false);
    }
}
