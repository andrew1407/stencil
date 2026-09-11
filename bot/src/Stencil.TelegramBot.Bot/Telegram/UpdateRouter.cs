using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Configuration;
using Telegram.Bot;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Bot.Telegram;

// Central inbound dispatch, and the only place AccessGate and UserGate are enforced. It owns
// the gating alone: AlbumRouter buffers media groups, MessageRouter runs the message-shape chain
// (command -> upload -> pending answer -> link -> chat mode -> hint), CallbackAction handles
// taps, and ErrorGuard wraps every body.
public sealed class UpdateRouter
{
    private readonly CallbackAction _callbacks;
    private readonly UserGate _gate;
    private readonly AccessGate _access;
    private readonly ErrorGuard _guard;
    private readonly AlbumRouter _albums;
    private readonly MessageRouter _messages;

    public UpdateRouter(
        CommandHandlers handlers,
        CallbackAction callbacks,
        IEditingService editing,
        ISessionStore store,
        ITelegramBotClient bot,
        UserGate gate,
        IBotPolicy options,
        ILogger<UpdateRouter> logger,
        AlbumCollector? albums = null)
    {
        _callbacks = callbacks;
        _gate = gate;
        _access = new AccessGate(options, bot, logger);
        _guard = new ErrorGuard(bot, logger);
        MediaIntake media = new(handlers, editing, store, bot, options);
        DocumentIntake documents = new(media, handlers, editing, store, bot);
        _albums = new AlbumRouter(media, handlers, store, bot, gate, _guard, albums ?? new AlbumCollector());
        _messages = new MessageRouter(handlers, media, documents, store, bot);
    }

    public async Task HandleMessageAsync(Message message, CancellationToken ct)
    {
        long chatId = message.Chat.Id;
        long userId = message.From?.Id ?? chatId;
        await _guard.RunAsync(chatId, async () =>
        {
            // The allowlist comes first, ahead of the album buffer: a stranger's media group is
            // never even collected, so no upload, CLI process or outbound fetch is spent on them.
            if (!IsUngatedMessage(message) && !await _access.AllowsAsync(userId, chatId, ct))
            {
                return;
            }
            // Album members buffer OUTSIDE the user gate: the flush acquires it itself, so waiting
            // for sibling messages while holding it would deadlock this user's queue.
            if (message.MediaGroupId is string groupId && message.Photo is { Length: > 0 } album)
            {
                _albums.Buffer(userId, chatId, groupId, message, album, ct);
                return;
            }
            using IDisposable gate = await _gate.AcquireAsync(userId, ct);
            await _messages.RouteAsync(new MessageContext(userId, chatId, message), ct);
        }, ct);
    }

    // The commands an unlisted user may still run (AccessGate).
    private static bool IsUngatedMessage(Message message) =>
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
            // Stop skips the USER gate on purpose: the assistant turn it cancels holds that gate
            // for as long as it runs, so taking it here would park the tap behind the very turn it
            // means to end. It touches no session state, so running it alongside the turn is safe.
            if (query.Data == CallbackAction.StopToken)
            {
                await _callbacks.HandleAsync(query, ct);
                return;
            }
            using IDisposable gate = await _gate.AcquireAsync(userId, ct);
            await _callbacks.HandleAsync(query, ct);
        }, ct);
    }
}
