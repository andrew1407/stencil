using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Configuration;
using Telegram.Bot;
using Telegram.Bot.Types;
using Stencil.TelegramBot.Bot.Telegram.Access;
using Stencil.TelegramBot.Bot.Telegram.Commands;
using Stencil.TelegramBot.Bot.Telegram.Intake;
using Stencil.TelegramBot.Bot.Telegram.Messaging;
using Stencil.TelegramBot.Bot.Telegram.Sync;

namespace Stencil.TelegramBot.Bot.Telegram;

// The only place AccessGate and UserGate are enforced; ErrorGuard wraps every body.
public sealed class UpdateRouter
{
    private readonly CallbackAction _callbacks;
    private readonly UserGate _gate;
    private readonly AccessGate _access;
    private readonly ErrorGuard _guard;
    private readonly AlbumRouter _albums;
    private readonly MessageRouter _messages;
    private readonly ISessionStore _store;
    private readonly SyncRegistry? _sync;

    public UpdateRouter(
        CommandHandlers handlers,
        CallbackAction callbacks,
        EditingService editing,
        ISessionStore store,
        ITelegramBotClient bot,
        UserGate gate,
        IBotPolicy options,
        ILogger<UpdateRouter> logger,
        AlbumCollector? albums = null,
        TimeProvider? clock = null,
        SyncRegistry? sync = null)
    {
        _store = store;
        _sync = sync;
        _callbacks = callbacks;
        _gate = gate;
        _access = new AccessGate(options, bot, logger, clock);
        _guard = new ErrorGuard(bot, logger);
        MediaIntake media = new(handlers, editing, store, bot, options);
        DocumentIntake documents = new(media, handlers, editing, store, bot);
        _albums = new AlbumRouter(media, handlers, store, bot, gate, _guard, albums ?? new AlbumCollector(options.AlbumSettle));
        _messages = new MessageRouter(handlers, media, documents, store, bot);
    }

    // The UpdatePump lane: the user this router gates on. Stop gets none, so it never waits behind the
    // turn it cancels.
    public static long LaneOf(Message message) => message.From?.Id ?? message.Chat.Id;

    public static long? LaneOf(CallbackQuery query) =>
        query.Data == CallbackAction.STOP_TOKEN ? null : query.From.Id;

    // Runs every album flush on the user's lane of the pump.
    public void UseLanes(Func<long, Func<Task>, Task> enqueue) => _albums.Lanes = enqueue;

    // Asked on the polling loop, before the pump: an unlisted sender's update never takes a pump slot.
    public async Task<bool> AdmitAsync(Message message, CancellationToken ct) =>
        isUngatedMessage(message) || await _access.AllowsAsync(LaneOf(message), message.Chat.Id, ct);

    public Task<bool> AdmitAsync(CallbackQuery query, CancellationToken ct) =>
        _access.AllowsAsync(query.From.Id, query.Message?.Chat.Id ?? query.From.Id, ct);

    public async Task HandleMessageAsync(Message message, CancellationToken ct)
    {
        long chatId = message.Chat.Id;
        long userId = LaneOf(message);
        await _guard.RunAsync(chatId, async () =>
        {
            // The allowlist comes before the album buffer: a stranger's media group is never even
            // collected.
            if (!isUngatedMessage(message) && !await _access.AllowsAsync(userId, chatId, ct))
            {
                return;
            }
            // Album members buffer OUTSIDE the user gate: the flush acquires it itself, so waiting
            // inside would deadlock.
            if (message.MediaGroupId is string groupId && message.Photo is { Length: > 0 } album)
            {
                _albums.Buffer(userId, chatId, groupId, message, album, ct);
                return;
            }
            using IDisposable gate = await _gate.AcquireAsync(userId, ct);
            await rejoinSyncAsync(userId, chatId, ct);
            await _messages.RouteAsync(new MessageContext(userId, chatId, message), ct);
        }, ct);
    }

    private static bool isUngatedMessage(Message message) =>
        message.Text is string text && text.StartsWith('/')
            && AccessGate.IsUngated(CommandParser.Parse(text));

    public async Task HandleUpdateAsync(Update update, CancellationToken ct)
    {
        if (update.CallbackQuery is not CallbackQuery query)
        {
            return;
        }
        long chatId = query.Message?.Chat.Id ?? query.From.Id;
        long userId = query.From.Id;
        await _guard.RunAsync(chatId, async () =>
        {
            // Every button does real work, so none of them is ungated.
            if (!await _access.AllowsAsync(userId, chatId, ct))
            {
                return;
            }
            // Stop skips the USER gate on purpose: the turn it cancels holds that gate; it touches
            // no session state.
            if (query.Data == CallbackAction.STOP_TOKEN)
            {
                await _callbacks.HandleAsync(query, ct);
                return;
            }
            using IDisposable gate = await _gate.AcquireAsync(userId, ct);
            await rejoinSyncAsync(userId, chatId, ct);
            await _callbacks.HandleAsync(query, ct);
        }, ct);
    }

    // The registry lives in memory while SyncEnabled persists with the session, so a restart
    // re-registers a synced chat on its next update.
    private async Task rejoinSyncAsync(long userId, long chatId, CancellationToken ct)
    {
        if (_sync is not null && await _store.GetAsync(userId, ct) is { SyncEnabled: true, ActiveProjectId: not null })
        {
            _sync.Rejoin(userId, chatId);
        }
    }
}
