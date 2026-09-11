using Stencil.TelegramBot.Domain.Abstractions;
using Telegram.Bot;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Bot.Telegram;

public sealed record MessageContext(long UserId, long ChatId, Message Message)
{
    public string? Text => Message.Text;
}

/// <summary>
/// One link of the message-shape chain: claim the message and return true, or decline and let
/// the next link try. Link ORDER is the precedence — see <see cref="MessageRouter.Chain"/>.
/// </summary>
public interface IMessageHandler
{
    Task<bool> TryHandleAsync(MessageContext ctx, CancellationToken ct);
}

/// <summary>
/// The message-shape precedence, as an executable chain rather than nested ifs: a slash command
/// short-circuits everything, then an upload (photo / video / document), then a pending
/// free-text answer, then a pasted http(s) link, then chat mode, and finally the
/// "send a photo" hint. Each link is a small class that owns exactly one shape.
/// </summary>
public sealed class MessageRouter
{
    private readonly IReadOnlyList<IMessageHandler> _chain;

    public MessageRouter(
        CommandHandlers handlers, MediaIntake media, DocumentIntake documents,
        ISessionStore store, ITelegramBotClient bot)
    {
        _chain = Chain(handlers, media, documents, store, bot);
    }

    /// <summary>The chain in precedence order — the one place the order is written down.</summary>
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
