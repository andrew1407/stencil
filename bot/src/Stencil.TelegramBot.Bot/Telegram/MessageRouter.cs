using Stencil.TelegramBot.Domain.Abstractions;
using Telegram.Bot;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Bot.Telegram;

public sealed record MessageContext(long UserId, long ChatId, Message Message)
{
    public string? Text => Message.Text;
}

// Claim the message and return true, or decline; link ORDER is the precedence
// (MessageRouter.Chain).
public interface IMessageHandler
{
    Task<bool> TryHandleAsync(MessageContext ctx, CancellationToken ct);
}

// Precedence: slash command, upload, pending free-text answer, pasted http(s) link, chat mode,
// "send a photo" hint.
public sealed class MessageRouter
{
    private readonly IReadOnlyList<IMessageHandler> _chain;

    public MessageRouter(
        CommandHandlers handlers, MediaIntake media, DocumentIntake documents,
        ISessionStore store, ITelegramBotClient bot)
    {
        _chain = Chain(handlers, media, documents, store, bot);
    }

    // The one place the order is written down.
    private static IReadOnlyList<IMessageHandler> Chain(
        CommandHandlers handlers, MediaIntake media, DocumentIntake documents,
        ISessionStore store, ITelegramBotClient bot) =>
    [
        new CommandLink(handlers, media, store),
        new UploadLink(media, documents, store),
        new PendingInputLink(handlers, store),
        new UrlLink(handlers, store, bot),
        new ChatModeLink(handlers, store),
        new FallbackLink(bot),
    ];

    public async Task RouteAsync(MessageContext ctx, CancellationToken ct)
    {
        foreach (IMessageHandler link in _chain)
        {
            if (await link.TryHandleAsync(ctx, ct))
            {
                return;
            }
        }
    }

    internal static async Task ClearPendingInputAsync(ISessionStore store, long userId, CancellationToken ct)
    {
        UserSession session = await store.GetAsync(userId, ct);
        if (session.PendingInput is not null)
        {
            await store.SaveAsync(session with { PendingInput = null }, ct);
        }
    }
}
