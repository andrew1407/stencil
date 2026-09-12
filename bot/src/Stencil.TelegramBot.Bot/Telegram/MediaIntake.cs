using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Configuration;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;

namespace Stencil.TelegramBot.Bot.Telegram;

// Every shape that brings pixels in funnels through WithDownloadedAsync, so the caps and temp-file
// cleanup are written once.
public sealed class MediaIntake
{
    private readonly CommandHandlers _handlers;
    private readonly IEditingService _editing;
    private readonly ISessionStore _store;
    private readonly ITelegramBotClient _bot;
    private readonly IBotPolicy _options;

    public MediaIntake(
        CommandHandlers handlers, IEditingService editing, ISessionStore store,
        ITelegramBotClient bot, IBotPolicy options)
    {
        _handlers = handlers;
        _editing = editing;
        _store = store;
        _bot = bot;
        _options = options;
    }

    // The temp file is deleted afterwards whatever happens; maxBytes defaults to the image/video
    // cap.
    public async Task WithDownloadedAsync(
        string fileId, string extension, Func<string, Task> use, CancellationToken ct, long? maxBytes = null)
    {
        string path = Path.Combine(Path.GetTempPath(), $"stencil-bot-{Guid.NewGuid():N}{extension}");
        try
        {
            try
            {
                await using FileStream stream = File.Create(path);
                await using CappingWriteStream capped = new(stream, maxBytes ?? _options.MaxDownloadBytes);
                await _bot.GetInfoAndDownloadFile(fileId, capped, ct);
            }
            catch
            {
                tryDelete(path);
                throw;
            }
            await use(path);
        }
        finally
        {
            tryDelete(path);
        }
    }

    public Task AdoptImageAsync(long userId, long chatId, string fileId, string extension, string label, string? caption, CancellationToken ct) =>
        WithDownloadedAsync(fileId, extension, async path =>
        {
            // A directly-uploaded photo/file has no http(s) origin, so leave SourceUrl unset.
            await _editing.SetImageFromLocalFileAsync(userId, path, label, ct: ct);
            await ApplyCaptionOrRenderAsync(userId, chatId, caption, ct);
        }, ct);

    // A caption /frame n selects the frame; any other caption applies to frame 0.
    public Task AdoptVideoAsync(long userId, long chatId, string fileId, string extension, string label, string? caption, CancellationToken ct) =>
        WithDownloadedAsync(fileId, extension, async path =>
        {
            (int frame, bool isFrameCaption) = parseFrameCaption(caption);
            await _editing.SetImageFromVideoAsync(userId, path, frame, label, ct);
            if (isFrameCaption)
            {
                await _handlers.RenderAndSendAsync(userId, chatId, ct);
            }
            else if (HasCaptionCommand(caption))
            {
                await ApplyCaptionOrRenderAsync(userId, chatId, caption, ct);
            }
            else
            {
                await _bot.SendMessage(chatId, "Grabbed frame 0 — use /frame n to pick another.", cancellationToken: ct);
                await _handlers.RenderAndSendAsync(userId, chatId, ct);
            }
        }, ct);

    public Task SetWorkingImageAsync(long userId, string path, string label, CancellationToken ct) =>
        _editing.SetImageFromLocalFileAsync(userId, path, label, ct: ct);

    // A recognised caption edit runs; a plain-text caption goes to the assistant when chat mode is
    // on; else render.
    public async Task ApplyCaptionOrRenderAsync(long userId, long chatId, string? caption, CancellationToken ct)
    {
        if (!HasCaptionCommand(caption))
        {
            if (!string.IsNullOrWhiteSpace(caption) && (await _store.GetAsync(userId, ct)).ChatMode)
            {
                await _handlers.DispatchAsync(userId, chatId, CommandParser.Prompt(caption!), ct);
                return;
            }
            await _handlers.RenderAndSendAsync(userId, chatId, ct);
            return;
        }
        BotCommand command = CommandParser.Parse(caption!);
        if (CaptionEditVerbs.Contains(command.Verb))
        {
            await _handlers.DispatchAsync(userId, chatId, command, ct);
        }
        else
        {
            await _handlers.RenderAndSendAsync(userId, chatId, ct);
        }
    }

    public static bool HasCaptionCommand(string? caption) =>
        caption is not null && caption.TrimStart().StartsWith('/');

    private static (int Frame, bool IsFrameCaption) parseFrameCaption(string? caption)
    {
        if (!HasCaptionCommand(caption))
        {
            return (0, false);
        }
        BotCommand command = CommandParser.Parse(caption!);
        if (command.Verb == "frame" && command.Args.Count >= 1 && int.TryParse(command.Args[0], out int n))
        {
            return (n, true);
        }
        return (0, false);
    }

    private static readonly HashSet<string> CaptionEditVerbs = new(StringComparer.Ordinal)
    {
        "crop", "rotate", "filter",
        "draw", "line", "polyline", "rect", "rectangle", "poly", "polygon",
        "reset", "undoline", "clearlines", "image", "json",
        "prompt",
    };

    // Streams to a temp file first, so an oversized upload is refused on the way to disk under the
    // tighter non-image cap.
    public async Task<byte[]> DownloadDocumentBytesAsync(string fileId, string extension, CancellationToken ct)
    {
        byte[] bytes = [];
        await WithDownloadedAsync(
            fileId, extension, async path => bytes = await File.ReadAllBytesAsync(path, ct), ct,
            _options.MaxDocumentBytes);
        return bytes;
    }

    private static void tryDelete(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch
        {
        }
    }
}
