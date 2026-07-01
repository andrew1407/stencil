using System.Text.Json;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Central inbound dispatch. Telegram messages are routed by shape: a slash command goes
/// through <see cref="CommandParser"/> + <see cref="CommandHandlers"/>; a photo or image
/// document becomes the working image; a <c>.json</c> document (caption <c>/apply</c>, or any
/// <c>.json</c> while an image is loaded) overlays a drawing layout; and callback queries go
/// through <see cref="CallbackAction"/>. Every handler body is guarded so a domain error is
/// surfaced verbatim and an unexpected one is logged and apologised for.
/// </summary>
public sealed class UpdateRouter
{
    private readonly CommandHandlers _handlers;
    private readonly CallbackAction _callbacks;
    private readonly IEditingService _editing;
    private readonly ISessionStore _store;
    private readonly ITelegramBotClient _bot;
    private readonly ILogger<UpdateRouter> _logger;

    public UpdateRouter(
        CommandHandlers handlers,
        CallbackAction callbacks,
        IEditingService editing,
        ISessionStore store,
        ITelegramBotClient bot,
        ILogger<UpdateRouter> logger)
    {
        _handlers = handlers;
        _callbacks = callbacks;
        _editing = editing;
        _store = store;
        _bot = bot;
        _logger = logger;
    }

    /// <summary>Route one incoming message (slash command, photo, or document).</summary>
    public async Task HandleMessageAsync(Message message, CancellationToken ct)
    {
        long chatId = message.Chat.Id;
        long userId = message.From?.Id ?? chatId;
        await SafeAsync(chatId, () => RouteMessageAsync(userId, chatId, message, ct), ct);
    }

    /// <summary>Route one non-message update — only callback queries are acted on here.</summary>
    public async Task HandleUpdateAsync(Update update, CancellationToken ct)
    {
        if (update.CallbackQuery is not CallbackQuery query)
        {
            return;
        }
        long chatId = query.Message?.Chat.Id ?? query.From.Id;
        await SafeAsync(chatId, () => _callbacks.HandleAsync(query, ct), ct);
    }

    /// <summary>The message-shape switch, run inside the error guard.</summary>
    private async Task RouteMessageAsync(long userId, long chatId, Message message, CancellationToken ct)
    {
        if (message.Text is string text && text.StartsWith('/'))
        {
            BotCommand command = CommandParser.Parse(text);
            await _handlers.DispatchAsync(userId, chatId, command, ct);
            return;
        }
        if (message.Photo is { Length: > 0 } photos)
        {
            await AdoptImageAsync(userId, chatId, photos[^1].FileId, ".jpg", "photo", message.Caption, ct);
            return;
        }
        if (message.Video is Video video)
        {
            await AdoptVideoAsync(userId, chatId, video.FileId, ExtensionOf(video.FileName ?? "", ".mp4"), "video", message.Caption, ct);
            return;
        }
        if (message.Document is Document document)
        {
            await HandleDocumentAsync(userId, chatId, document, message.Caption, ct);
            return;
        }
        // A pasted http(s) link (no command, no attachment) is treated as /url — fetch it.
        if (message.Text is string body && TryExtractUrl(body, out string url))
        {
            await _handlers.DispatchAsync(userId, chatId, new BotCommand("url", url, [url]), ct);
            return;
        }
        if (!string.IsNullOrWhiteSpace(message.Text))
        {
            await _bot.SendMessage(chatId, "Send a photo or an image link to edit, or /help for commands.", cancellationToken: ct);
        }
    }

    /// <summary>Find the first http(s) URL token in a message body (for bare-link image loads).</summary>
    private static bool TryExtractUrl(string text, out string url)
    {
        url = "";
        foreach (string token in text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries))
        {
            if (token.StartsWith("http://", StringComparison.OrdinalIgnoreCase)
                || token.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
            {
                url = token;
                return true;
            }
        }
        return false;
    }

    /// <summary>
    /// Download a Telegram image (compressed photo or uncompressed file) and adopt it as the
    /// working image, then apply any caption command (e.g. <c>/crop …</c>) or just render it.
    /// </summary>
    private async Task AdoptImageAsync(long userId, long chatId, string fileId, string extension, string label, string? caption, CancellationToken ct)
    {
        string path = await DownloadToTempAsync(fileId, extension, ct);
        try
        {
            await _editing.SetImageFromLocalFileAsync(userId, path, label, ct);
            await ApplyCaptionOrRenderAsync(userId, chatId, caption, ct);
        }
        finally
        {
            TryDelete(path);
        }
    }

    /// <summary>
    /// Download a Telegram video (compressed or as a file) and grab a frame. A caption
    /// <c>/frame n</c> selects the frame; any other caption command (e.g. <c>/filter bw</c>) is
    /// applied to frame 0; with no caption it grabs frame 0 and hints at <c>/frame n</c>.
    /// </summary>
    private async Task AdoptVideoAsync(long userId, long chatId, string fileId, string extension, string label, string? caption, CancellationToken ct)
    {
        string path = await DownloadToTempAsync(fileId, extension, ct);
        try
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
        }
        finally
        {
            TryDelete(path);
        }
    }

    /// <summary>
    /// After adopting an upload, apply a caption command when it is a recognised edit (it then
    /// renders the result), otherwise just render the adopted image.
    /// </summary>
    private async Task ApplyCaptionOrRenderAsync(long userId, long chatId, string? caption, CancellationToken ct)
    {
        if (!HasCaptionCommand(caption))
        {
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

    /// <summary>True when a caption is a slash command.</summary>
    private static bool HasCaptionCommand(string? caption) =>
        caption is not null && caption.TrimStart().StartsWith('/');

    /// <summary>If the caption is <c>/frame n</c>, the frame index and true; otherwise (0, false).</summary>
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

    /// <summary>Caption verbs that edit the just-uploaded image and produce a rendered result.</summary>
    private static readonly HashSet<string> CaptionEditVerbs = new(StringComparer.Ordinal)
    {
        "crop", "rotate", "filter",
        "draw", "line", "polyline", "rect", "rectangle", "poly", "polygon",
        "reset", "undoline", "clearlines", "image", "json",
    };

    /// <summary>
    /// Handle an uploaded document: a <c>.json</c> layout (apply), an image, or a video file.
    /// Unknown document types get a hint.
    /// </summary>
    private async Task HandleDocumentAsync(long userId, long chatId, Document document, string? caption, CancellationToken ct)
    {
        string name = document.FileName ?? "";
        bool isJson = name.EndsWith(".json", StringComparison.OrdinalIgnoreCase)
            || string.Equals(document.MimeType, "application/json", StringComparison.OrdinalIgnoreCase);
        if (isJson)
        {
            await ApplyLayoutDocumentAsync(userId, chatId, document.FileId, caption, ct);
            return;
        }
        if (IsImageDocument(document))
        {
            string ext = ExtensionOf(name, ".png");
            string label = name.Length == 0 ? "image" : name;
            await AdoptImageAsync(userId, chatId, document.FileId, ext, label, caption, ct);
            return;
        }
        if (IsVideoDocument(document))
        {
            string ext = ExtensionOf(name, ".mp4");
            string label = name.Length == 0 ? "video" : name;
            await AdoptVideoAsync(userId, chatId, document.FileId, ext, label, caption, ct);
            return;
        }
        await _bot.SendMessage(
            chatId,
            "Unsupported file. Send an image or video to edit, or a .json layout with caption /apply.",
            cancellationToken: ct);
    }

    /// <summary>Download a <c>.json</c> document, parse it as a layout and apply it, then render.</summary>
    private async Task ApplyLayoutDocumentAsync(long userId, long chatId, string fileId, string? caption, CancellationToken ct)
    {
        bool explicitApply = string.Equals(caption?.Trim(), "/apply", StringComparison.OrdinalIgnoreCase);
        UserSession session = await _store.GetAsync(userId, ct);
        if (!explicitApply && !session.HasImage)
        {
            await _bot.SendMessage(
                chatId,
                "Send an image first, then upload a .json layout (or add the caption /apply).",
                cancellationToken: ct);
            return;
        }
        byte[] bytes = await DownloadBytesAsync(fileId, ct);
        StencilLayout? layout = ParseLayout(bytes);
        if (layout is null)
        {
            await _bot.SendMessage(chatId, "That file isn't a valid Stencil layout JSON.", cancellationToken: ct);
            return;
        }
        await _editing.ApplyLayoutAsync(userId, layout, ct);
        await _handlers.RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>Parse layout bytes via the shared JSON conventions (null on malformed input).</summary>
    private static StencilLayout? ParseLayout(byte[] bytes)
    {
        try
        {
            using JsonDocument document = JsonDocument.Parse(bytes);
            return StencilJson.FromElement<StencilLayout>(document.RootElement);
        }
        catch (JsonException)
        {
            return null;
        }
    }

    /// <summary>Download a Telegram file's bytes into memory.</summary>
    private async Task<byte[]> DownloadBytesAsync(string fileId, CancellationToken ct)
    {
        using MemoryStream stream = new();
        await _bot.GetInfoAndDownloadFile(fileId, stream, ct);
        return stream.ToArray();
    }

    /// <summary>Download a Telegram file to a fresh temp path with the given extension.</summary>
    private async Task<string> DownloadToTempAsync(string fileId, string extension, CancellationToken ct)
    {
        string path = Path.Combine(Path.GetTempPath(), $"stencil-bot-{Guid.NewGuid():N}{extension}");
        await using (FileStream stream = File.Create(path))
        {
            await _bot.GetInfoAndDownloadFile(fileId, stream, ct);
        }
        return path;
    }

    /// <summary>Run an action, surfacing domain errors verbatim and logging unexpected ones.</summary>
    private async Task SafeAsync(long chatId, Func<Task> action, CancellationToken ct)
    {
        try
        {
            await action();
        }
        catch (InvalidOperationException ex)
        {
            await ReplyError(chatId, ex.Message, ct);
        }
        catch (ServerException ex)
        {
            await ReplyError(chatId, ex.Message, ct);
        }
        catch (StencilCliException ex)
        {
            await ReplyError(chatId, ex.Message, ct);
        }
        catch (OperationCanceledException)
        {
            // Shutdown in progress — let it unwind quietly.
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Unexpected error handling update for chat {ChatId}", chatId);
            await ReplyError(chatId, "Sorry, something went wrong handling that. Please try again.", ct);
        }
    }

    /// <summary>Best-effort error reply (a failed reply must not mask the original error).</summary>
    private async Task ReplyError(long chatId, string message, CancellationToken ct)
    {
        try
        {
            await _bot.SendMessage(chatId, message, cancellationToken: ct);
        }
        catch (Exception ex)
        {
            _logger.LogWarning(ex, "Failed to send error reply to chat {ChatId}", chatId);
        }
    }

    /// <summary>Treat a document as an image by MIME type or by a known image extension.</summary>
    private static bool IsImageDocument(Document document)
    {
        if (document.MimeType is string mime && mime.StartsWith("image/", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }
        string ext = ExtensionOf(document.FileName ?? "", "");
        return ext is ".png" or ".jpg" or ".jpeg" or ".gif" or ".bmp" or ".webp" or ".tif" or ".tiff";
    }

    /// <summary>Treat a document as a video by MIME type or by a known video extension.</summary>
    private static bool IsVideoDocument(Document document)
    {
        if (document.MimeType is string mime && mime.StartsWith("video/", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }
        string ext = ExtensionOf(document.FileName ?? "", "");
        return ext is ".mp4" or ".mov" or ".webm" or ".mkv" or ".avi" or ".m4v";
    }

    /// <summary>The lowercased extension of a file name, or a fallback when none is present.</summary>
    private static string ExtensionOf(string name, string fallback)
    {
        string ext = Path.GetExtension(name);
        return ext.Length == 0 ? fallback : ext.ToLowerInvariant();
    }

    /// <summary>Delete a temp file, ignoring failures.</summary>
    private static void TryDelete(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch
        {
            // Best effort — a leftover temp file is harmless.
        }
    }
}
