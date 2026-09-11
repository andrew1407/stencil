using Stencil.TelegramBot.Domain.Abstractions;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// A slash command — the highest-precedence shape, short-circuiting every plain-text rule. It
/// supersedes any pending free-text prompt (e.g. the custom-expiry entry).
/// </summary>
public sealed class CommandLink : IMessageHandler
{
    private readonly CommandHandlers _handlers;
    private readonly MediaIntake _media;
    private readonly ISessionStore _store;

    public CommandLink(CommandHandlers handlers, MediaIntake media, ISessionStore store)
    {
        _handlers = handlers;
        _media = media;
        _store = store;
    }

    public async Task<bool> TryHandleAsync(MessageContext ctx, CancellationToken ct)
    {
        if (ctx.Text is not string text || !text.StartsWith('/'))
        {
            return false;
        }
        await MessageRouter.ClearPendingInputAsync(_store, ctx.UserId, ct);
        BotCommand command = CommandParser.Parse(text);
        // "/prompt …" as a REPLY to a photo means "ask the AI about THAT photo": adopt it as
        // the working image first, then let the handler attach it to the LLM turn.
        if (command.Verb == "prompt" && ctx.Message.ReplyToMessage?.Photo is { Length: > 0 } replied)
        {
            await _media.WithDownloadedAsync(replied[^1].FileId, ".jpg",
                path => _media.SetWorkingImageAsync(ctx.UserId, path, "photo", ct), ct);
        }
        await _handlers.DispatchAsync(ctx.UserId, ctx.ChatId, command, ct);
        return true;
    }
}

/// <summary>
/// A photo, a video or a document. An upload also supersedes a pending free-text prompt — only
/// a plain-text reply answers one.
/// </summary>
public sealed class UploadLink : IMessageHandler
{
    private readonly MediaIntake _media;
    private readonly DocumentIntake _documents;
    private readonly ISessionStore _store;

    public UploadLink(MediaIntake media, DocumentIntake documents, ISessionStore store)
    {
        _media = media;
        _documents = documents;
        _store = store;
    }

    public async Task<bool> TryHandleAsync(MessageContext ctx, CancellationToken ct)
    {
        Message message = ctx.Message;
        if (message.Photo is not { Length: > 0 } && message.Video is null && message.Document is null)
        {
            return false;
        }
        await MessageRouter.ClearPendingInputAsync(_store, ctx.UserId, ct);
        if (message.Photo is { Length: > 0 } photos)
        {
            await _media.AdoptImageAsync(ctx.UserId, ctx.ChatId, photos[^1].FileId, ".jpg", "photo", message.Caption, ct);
        }
        else if (message.Video is Video video)
        {
            await _media.AdoptVideoAsync(ctx.UserId, ctx.ChatId, video.FileId,
                DocumentKinds.ExtensionOf(video.FileName ?? "", ".mp4"), "video", message.Caption, ct);
        }
        else
        {
            await _documents.HandleAsync(ctx.UserId, ctx.ChatId, message.Document!, message.Caption, ct);
        }
        return true;
    }
}
