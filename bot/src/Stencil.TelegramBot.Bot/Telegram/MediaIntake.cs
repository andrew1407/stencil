using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Configuration;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Telegram download + adopt plumbing: stream a file id to a capped temp path, make it the
/// working image (or a video frame), then run whatever its caption asked for. Every shape that
/// brings pixels in — a photo, a video, an image/video document, an album member, a
/// <c>/prompt</c> reply — funnels through <see cref="WithDownloadedAsync"/>, so the caps and the
/// temp-file cleanup are written once.
/// </summary>
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

    /// <summary>
    /// Download a Telegram file to a fresh temp path, hand that path to <paramref name="use"/>,
    /// and delete it afterwards whatever happens. <paramref name="maxBytes"/> defaults to the
    /// image/video cap; a partial file from a failed download is cleaned up too.
    /// </summary>
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
                TryDelete(path);
                throw;
            }
            await use(path);
        }
        finally
        {
            TryDelete(path);
        }
    }

    /// <summary>
    /// Download a Telegram image and adopt it as the working image, then apply any caption command
    /// (e.g. <c>/crop …</c>) or just render it.
    /// </summary>
    public Task AdoptImageAsync(long userId, long chatId, string fileId, string extension, string label, string? caption, CancellationToken ct) =>
        WithDownloadedAsync(fileId, extension, async path =>
        {
            // A directly-uploaded photo/file has no http(s) origin, so leave SourceUrl unset.
            await _editing.SetImageFromLocalFileAsync(userId, path, label, ct: ct);
            await ApplyCaptionOrRenderAsync(userId, chatId, caption, ct);
        }, ct);

    /// <summary>
    /// Download a Telegram video and grab a frame. A caption <c>/frame n</c> selects it; any other
    /// caption command applies to frame 0; with no caption it grabs frame 0 and hints at the flag.
    /// </summary>
    public Task AdoptVideoAsync(long userId, long chatId, string fileId, string extension, string label, string? caption, CancellationToken ct) =>
        WithDownloadedAsync(fileId, extension, async path =>
        {
            (int frame, bool isFrameCaption) = ParseFrameCaption(caption);
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

    /// <summary>
    /// After adopting an upload: run a recognised caption edit, else hand a plain-text caption to
    /// the assistant when chat mode is on, else just render the adopted image.
    /// </summary>
    public async Task ApplyCaptionOrRenderAsync(long userId, long chatId, string? caption, CancellationToken ct)
    {
        if (!HasCaptionCommand(caption))
        {
            // Chat mode: a plain-text caption is an assistant request about the adopted image.
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

    private static (int Frame, bool IsFrameCaption) ParseFrameCaption(string? caption)
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

    /// <summary>
    /// A document (layout <c>.json</c> / <c>.stencil</c> project) as bytes. It streams to a temp
    /// file first, so an oversized upload is refused on the way to disk rather than growing the
    /// heap, under the far tighter non-image cap <see cref="IBotPolicy.MaxDocumentBytes"/>.
    /// </summary>
    public async Task<byte[]> DownloadDocumentBytesAsync(string fileId, string extension, CancellationToken ct)
    {
        byte[] bytes = [];
        await WithDownloadedAsync(
            fileId, extension, async path => bytes = await File.ReadAllBytesAsync(path, ct), ct,
            _options.MaxDocumentBytes);
        return bytes;
    }

    /// <summary>Delete a throwaway download, ignoring failures — a leftover temp file is harmless.</summary>
    private static void TryDelete(string path)
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
