using System.Text.Json;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Project;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Telegram.Bot;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Central inbound dispatch, and where the global <see cref="AccessGate"/> is enforced. Telegram
/// messages are routed by shape: a slash command goes through <see cref="CommandParser"/> +
/// <see cref="CommandHandlers"/>; a photo or image document becomes the working image; a
/// <c>.json</c> document overlays a drawing layout; and callback queries go through
/// <see cref="CallbackAction"/>. Plain text is claimed in a fixed precedence: a pending free-text
/// prompt, then an http(s) link (as <c>/url</c>), then chat mode, then the "send a photo" hint —
/// slash commands short-circuit all of it. Every handler body is guarded so a domain error is
/// surfaced verbatim and an unexpected one is logged and apologised for.
/// </summary>
public sealed class UpdateRouter
{
    private readonly CommandHandlers _handlers;
    private readonly CallbackAction _callbacks;
    private readonly IEditingService _editing;
    private readonly ISessionStore _store;
    private readonly ITelegramBotClient _bot;
    private readonly UserGate _gate;
    private readonly BotOptions _options;
    private readonly ILogger<UpdateRouter> _logger;
    private readonly AlbumCollector _albums;
    private readonly AccessGate _access;

    public UpdateRouter(
        CommandHandlers handlers,
        CallbackAction callbacks,
        IEditingService editing,
        ISessionStore store,
        ITelegramBotClient bot,
        UserGate gate,
        BotOptions options,
        ILogger<UpdateRouter> logger,
        AlbumCollector? albums = null)
    {
        _handlers = handlers;
        _callbacks = callbacks;
        _editing = editing;
        _store = store;
        _bot = bot;
        _gate = gate;
        _options = options;
        _logger = logger;
        _albums = albums ?? new AlbumCollector();
        _access = new AccessGate(options, bot, logger);
    }

    /// <summary>
    /// Route one incoming message (slash command, photo, or document). The route runs under the
    /// user's <see cref="UserGate"/>, so a burst from one user is processed one update at a time
    /// and its read-modify-write edits can't race; different users stay concurrent.
    /// </summary>
    public async Task HandleMessageAsync(Message message, CancellationToken ct)
    {
        long chatId = message.Chat.Id;
        long userId = message.From?.Id ?? chatId;
        await SafeAsync(chatId, async () =>
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
                _albums.Add(userId, groupId,
                    new AlbumPhoto(message.Id, album[^1].FileId, message.Caption),
                    photos => FlushAlbumAsync(userId, chatId, photos, ct), ct);
                return;
            }
            using IDisposable gate = await _gate.AcquireAsync(userId, ct);
            await RouteMessageAsync(userId, chatId, message, ct);
        }, ct);
    }

    /// <summary>The two commands an unlisted user may still run (<see cref="AccessGate"/>).</summary>
    private static bool IsUngatedMessage(Message message) =>
        message.Text is string text && text.StartsWith('/')
            && AccessGate.IsUngated(CommandParser.Parse(text));

    /// <summary>A settled album's background flush: same error guard + user gate as a message.</summary>
    private Task FlushAlbumAsync(long userId, long chatId, IReadOnlyList<AlbumPhoto> photos, CancellationToken ct) =>
        SafeAsync(chatId, async () =>
        {
            using IDisposable gate = await _gate.AcquireAsync(userId, ct);
            await ProcessAlbumAsync(userId, chatId, photos, ct);
        }, ct);

    /// <summary>
    /// One settled album. With a caption (on whichever member carries it): adopt + run it once per
    /// photo in album order, buffering each render so the batch replies as ONE media group; the
    /// last photo's result stays as the working image. With no caption, only the last photo is
    /// adopted, with a single note — captionless members are never individually echoed.
    /// </summary>
    private async Task ProcessAlbumAsync(long userId, long chatId, IReadOnlyList<AlbumPhoto> photos, CancellationToken ct)
    {
        List<AlbumPhoto> ordered = photos.OrderBy(p => p.MessageId).ToList();
        await ClearPendingInputAsync(userId, ct);
        string? caption = ordered.FirstOrDefault(p => !string.IsNullOrWhiteSpace(p.Caption))?.Caption;
        if (caption is null)
        {
            await _bot.SendMessage(
                chatId,
                $"Got an album of {ordered.Count} photos — only one can be the working image, so I took the last. Caption an album to edit every photo.",
                cancellationToken: ct);
            await AdoptImageAsync(userId, chatId, ordered[^1].FileId, ".jpg", "photo", caption: null, ct);
            return;
        }
        List<PromptRender> results = new();
        for (int i = 0; i < ordered.Count; i++)
        {
            string label = $"photo {i + 1}/{ordered.Count}";
            string path = await DownloadToTempAsync(ordered[i].FileId, ".jpg", ct);
            try
            {
                await _editing.SetImageFromLocalFileAsync(userId, path, label, ct: ct);
                List<PromptRender> captured = new();
                using (_handlers.BeginRenderCapture(userId, captured))
                {
                    await ApplyCaptionOrRenderAsync(userId, chatId, caption, ct);
                }
                // A caption run can render more than once; the last render is this photo's result.
                if (captured.Count > 0)
                {
                    results.Add(captured[^1]);
                }
            }
            finally
            {
                TempFiles.TryDelete(path);
            }
        }
        await _handlers.SendRenderAlbumAsync(chatId, results, ct);
    }

    /// <summary>Route one non-message update — only callback queries are acted on here.</summary>
    public async Task HandleUpdateAsync(Update update, CancellationToken ct)
    {
        if (update.CallbackQuery is not CallbackQuery query)
        {
            return;
        }
        long chatId = query.Message?.Chat.Id ?? query.From.Id;
        long userId = query.From.Id;
        await SafeAsync(chatId, async () =>
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

    /// <summary>The message-shape switch, run inside the error guard.</summary>
    private async Task RouteMessageAsync(long userId, long chatId, Message message, CancellationToken ct)
    {
        if (message.Text is string text && text.StartsWith('/'))
        {
            // A command supersedes any pending free-text prompt (e.g. the custom-expiry entry).
            await ClearPendingInputAsync(userId, ct);
            BotCommand command = CommandParser.Parse(text);
            // "/prompt …" as a REPLY to a photo means "ask the AI about THAT photo": adopt it as
            // the working image first, then let the handler attach it to the LLM turn.
            if (command.Verb == "prompt" && message.ReplyToMessage?.Photo is { Length: > 0 } replied)
            {
                string repliedPath = await DownloadToTempAsync(replied[^1].FileId, ".jpg", ct);
                try
                {
                    await _editing.SetImageFromLocalFileAsync(userId, repliedPath, "photo", ct: ct);
                }
                finally
                {
                    TempFiles.TryDelete(repliedPath);
                }
            }
            await _handlers.DispatchAsync(userId, chatId, command, ct);
            return;
        }
        // An upload also supersedes a pending free-text prompt (only a plain-text reply answers it).
        if (message.Photo is { Length: > 0 } photos)
        {
            await ClearPendingInputAsync(userId, ct);
            await AdoptImageAsync(userId, chatId, photos[^1].FileId, ".jpg", "photo", message.Caption, ct);
            return;
        }
        if (message.Video is Video video)
        {
            await ClearPendingInputAsync(userId, ct);
            await AdoptVideoAsync(userId, chatId, video.FileId, DocumentKinds.ExtensionOf(video.FileName ?? "", ".mp4"), "video", message.Caption, ct);
            return;
        }
        if (message.Document is Document document)
        {
            await ClearPendingInputAsync(userId, ct);
            await HandleDocumentAsync(userId, chatId, document, message.Caption, ct);
            return;
        }
        // A plain-text reply to a pending prompt (e.g. the custom-expiry entry) is consumed here.
        if (message.Text is string reply && await TryConsumePendingInputAsync(userId, chatId, reply, ct))
        {
            return;
        }
        // A pasted http(s) link (no command, no attachment) is treated as /url — fetch it. Words
        // AROUND the link are a request about it, so in chat mode the link loads first and the
        // rest goes to the assistant. Without chat mode the link alone still wins.
        if (message.Text is string body && TryExtractUrl(body, out string url))
        {
            await _handlers.DispatchAsync(userId, chatId, new BotCommand("url", url, [url]), ct);
            string around = body.Replace(url, " ", StringComparison.Ordinal).Trim();
            if (around.Length > 0 && (await _store.GetAsync(userId, ct)).ChatMode)
            {
                // The /url render above already answered with the edit menu, which reads as
                // "done" — say the edit is still coming before the assistant turn runs (which
                // posts its own spinning notice for how long it takes).
                await _bot.SendMessage(chatId, "✏️ Loaded — now editing per your request…", cancellationToken: ct);
                await _handlers.DispatchAsync(userId, chatId, CommandParser.Prompt(around), ct);
            }
            return;
        }
        // Chat mode (/chat): anything left over — not a command, attachment, pending-prompt answer
        // or bare image link — is handed to the assistant exactly as "/prompt <text>" would be.
        if (message.Text is string chat && !string.IsNullOrWhiteSpace(chat)
            && (await _store.GetAsync(userId, ct)).ChatMode)
        {
            await _handlers.DispatchAsync(userId, chatId, CommandParser.Prompt(chat), ct);
            return;
        }
        if (!string.IsNullOrWhiteSpace(message.Text))
        {
            await _bot.SendMessage(chatId, "Send a photo or an image link to edit, or /help for commands.", cancellationToken: ct);
        }
    }

    /// <summary>Clear a pending free-text prompt, if one is armed (a no-op otherwise).</summary>
    private async Task ClearPendingInputAsync(long userId, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (session.PendingInput is not null)
        {
            await _store.SaveAsync(session with { PendingInput = null }, ct);
        }
    }

    /// <summary>
    /// Consume a plain-text message as the answer to an armed prompt and dispatch the matching
    /// command (one-shot — the flag is cleared first). Returns whether it was consumed.
    /// </summary>
    private async Task<bool> TryConsumePendingInputAsync(long userId, long chatId, string reply, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        string? pending = session.PendingInput;
        if (pending is not (PendingInputs.ExpiryDuration or PendingInputs.ProjectName or PendingInputs.ProjectDescription))
        {
            return false;
        }
        await _store.SaveAsync(session with { PendingInput = null }, ct);
        string spec = reply.Trim();
        string[] args = spec.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        BotCommand command = pending switch
        {
            // The whole reply is the new name (names may contain spaces), so pass it verbatim.
            PendingInputs.ProjectName => new BotCommand("projectname", spec, args),
            // The whole reply is the description; a lone "-" is the clear convention (→ empty).
            PendingInputs.ProjectDescription => spec == "-"
                ? new BotCommand("projectdescription", "", [])
                : new BotCommand("projectdescription", spec, args),
            _ => new BotCommand("expire", spec, args),
        };
        await _handlers.DispatchAsync(userId, chatId, command, ct);
        return true;
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
    /// Download a Telegram image and adopt it as the working image, then apply any caption command
    /// (e.g. <c>/crop …</c>) or just render it.
    /// </summary>
    private async Task AdoptImageAsync(long userId, long chatId, string fileId, string extension, string label, string? caption, CancellationToken ct)
    {
        string path = await DownloadToTempAsync(fileId, extension, ct);
        try
        {
            // A directly-uploaded photo/file has no http(s) origin, so leave SourceUrl unset.
            await _editing.SetImageFromLocalFileAsync(userId, path, label, ct: ct);
            await ApplyCaptionOrRenderAsync(userId, chatId, caption, ct);
        }
        finally
        {
            TempFiles.TryDelete(path);
        }
    }

    /// <summary>
    /// Download a Telegram video and grab a frame. A caption <c>/frame n</c> selects it; any other
    /// caption command applies to frame 0; with no caption it grabs frame 0 and hints at the flag.
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
            TempFiles.TryDelete(path);
        }
    }

    /// <summary>
    /// After adopting an upload: run a recognised caption edit, else hand a plain-text caption to
    /// the assistant when chat mode is on, else just render the adopted image.
    /// </summary>
    private async Task ApplyCaptionOrRenderAsync(long userId, long chatId, string? caption, CancellationToken ct)
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
        "prompt",
    };

    /// <summary>Handle an uploaded document: a <c>.json</c> layout, an image, a video, or a hint.</summary>
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
        if (name.EndsWith(".stencil", StringComparison.OrdinalIgnoreCase))
        {
            await OpenProjectDocumentAsync(userId, chatId, document.FileId, ct);
            return;
        }
        if (DocumentKinds.IsImage(document))
        {
            string ext = DocumentKinds.ExtensionOf(name, ".png");
            string label = name.Length == 0 ? "image" : name;
            await AdoptImageAsync(userId, chatId, document.FileId, ext, label, caption, ct);
            return;
        }
        if (DocumentKinds.IsVideo(document))
        {
            string ext = DocumentKinds.ExtensionOf(name, ".mp4");
            string label = name.Length == 0 ? "video" : name;
            await AdoptVideoAsync(userId, chatId, document.FileId, ext, label, caption, ct);
            return;
        }
        await _bot.SendMessage(
            chatId,
            "Unsupported file. Send an image or video to edit, or a .json layout with caption /apply.",
            cancellationToken: ct);
    }

    /// <summary>Parse a <c>.json</c> document as a layout, apply it, then render.</summary>
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
        byte[] bytes = await DownloadDocumentBytesAsync(fileId, ".json", ct);
        StencilLayout? layout = StencilLayoutParser.Parse(bytes);
        if (layout is null)
        {
            await _bot.SendMessage(chatId, "That file isn't a valid Stencil layout JSON.", cancellationToken: ct);
            return;
        }
        await _editing.ApplyLayoutAsync(userId, layout, ct: ct);
        await _handlers.RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>Adopt a <c>.stencil</c> project document (image + layout), then render.</summary>
    private async Task OpenProjectDocumentAsync(long userId, long chatId, string fileId, CancellationToken ct)
    {
        byte[] bytes = await DownloadDocumentBytesAsync(fileId, ".stencil", ct);
        StencilProject? project = StencilProjectFile.Parse(bytes);
        if (project is null)
        {
            await _bot.SendMessage(chatId, "That file isn't a valid .stencil project.", cancellationToken: ct);
            return;
        }
        await _editing.OpenProjectFileAsync(userId, project, ct);
        await _handlers.RenderAndSendAsync(userId, chatId, ct);
    }

    /// <summary>
    /// A document (layout <c>.json</c> / <c>.stencil</c> project) as bytes. It streams to a temp
    /// file first, so an oversized upload is refused on the way to disk rather than growing the
    /// heap, under the far tighter non-image cap <see cref="BotOptions.MaxDocumentBytes"/>.
    /// </summary>
    private async Task<byte[]> DownloadDocumentBytesAsync(string fileId, string extension, CancellationToken ct)
    {
        string path = await DownloadToTempAsync(fileId, extension, ct, _options.MaxDocumentBytes);
        try
        {
            return await File.ReadAllBytesAsync(path, ct);
        }
        finally
        {
            TempFiles.TryDelete(path);
        }
    }

    /// <summary>
    /// Download a Telegram file to a fresh temp path, capped at <paramref name="maxBytes"/> (the
    /// image/video limit by default). A partial file from a failed download is cleaned up.
    /// </summary>
    private async Task<string> DownloadToTempAsync(string fileId, string extension, CancellationToken ct, long? maxBytes = null)
    {
        string path = Path.Combine(Path.GetTempPath(), $"stencil-bot-{Guid.NewGuid():N}{extension}");
        try
        {
            await using FileStream stream = File.Create(path);
            await using CappingWriteStream capped = new(stream, maxBytes ?? _options.MaxDownloadBytes);
            await _bot.GetInfoAndDownloadFile(fileId, capped, ct);
        }
        catch
        {
            TempFiles.TryDelete(path);
            throw;
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
            // A deployment fault tells the chat a sentence and the operator the whole story.
            if (ex.OperatorDetail is string detail)
            {
                _logger.LogError("Stencil CLI unavailable: {Detail}", detail);
            }
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

    /// <summary>
    /// Best-effort error reply (a failed reply must not mask the original error). Every failure
    /// the bot answers with wears the error glyph here.
    /// </summary>
    private async Task ReplyError(long chatId, string message, CancellationToken ct)
    {
        try
        {
            await _bot.SendMessage(chatId, Replies.Tag(Replies.Tone.Error, message), cancellationToken: ct);
        }
        catch (Exception ex)
        {
            _logger.LogWarning(ex, "Failed to send error reply to chat {ChatId}", chatId);
        }
    }
}
