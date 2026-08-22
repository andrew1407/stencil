using System.Collections.Concurrent;
using System.Globalization;
using System.Text;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Links;
using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.Enums;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// One handler per slash command, plus the shared render-and-send helper. Each command folds its
/// intent through <see cref="IEditingService"/> / <see cref="IServerService"/> — the same
/// Application services the callback buttons use — then replies over Telegram. Split by command
/// group into partial files: Projects, Editing, Sources, and Assistant (LLM).
/// </summary>
public sealed partial class CommandHandlers
{
    private readonly IEditingService _editing;
    private readonly IServerService _servers;
    private readonly ISessionStore _store;
    private readonly ITelegramBotClient _bot;
    private readonly BotOptions _options;
    private readonly SyncRegistry _sync;
    private readonly LayoutFetcher _layoutFetcher;
    private readonly PromptService _prompts;
    private readonly LlmAttachmentLoader _attachments;
    private readonly PromptCancellations _cancellations;
    private readonly ILogger<CommandHandlers> _logger;
    // User ids whose assistant refusal already carried the operator hint to the log.
    private readonly ConcurrentDictionary<long, byte> _refusalsLogged = new();

    public CommandHandlers(
        IEditingService editing,
        IServerService servers,
        ISessionStore store,
        ITelegramBotClient bot,
        BotOptions options,
        SyncRegistry sync,
        LayoutFetcher layoutFetcher,
        PromptService prompts,
        LlmAttachmentLoader attachments,
        PromptCancellations cancellations,
        ILogger<CommandHandlers> logger)
    {
        _editing = editing;
        _servers = servers;
        _store = store;
        _bot = bot;
        _options = options;
        _sync = sync;
        _layoutFetcher = layoutFetcher;
        _prompts = prompts;
        _attachments = attachments;
        _cancellations = cancellations;
        _logger = logger;
    }

    /// <summary>Route a parsed command to its handler (unknown verbs fall back to a /help hint).</summary>
    public Task DispatchAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct) =>
        cmd.Verb switch
        {
            "start" => StartAsync(userId, chatId, cmd, ct),
            "help" => HelpAsync(chatId, ct),
            // "/p" is normalised to "prompt" by CommandParser, so one verb covers both.
            "prompt" => PromptAsync(userId, chatId, cmd, ct),
            "chat" => ChatAsync(userId, chatId, cmd, ct),
            "chatapi" => ChatApiAsync(userId, chatId, cmd, ct),
            "connect" => ConnectAsync(userId, chatId, cmd, ct),
            "disconnect" => DisconnectAsync(userId, chatId, cmd, ct),
            "connections" => ConnectionsAsync(userId, chatId, cmd, ct),
            "projects" => ProjectsAsync(userId, chatId, cmd, ct),
            "fetch" => FetchAsync(userId, chatId, cmd, ct),
            "create" => CreateAsync(userId, chatId, cmd, ct),
            "save" => SaveAsync(userId, chatId, ct),
            "sync" => SyncAsync(userId, chatId, cmd, ct),
            "projectcolor" or "project_color" or "project-color" or "pcolor" => ProjectColorAsync(userId, chatId, cmd, ct),
            "projectname" or "project_name" or "project-name" or "pname" or "rename" => ProjectNameAsync(userId, chatId, cmd, ct),
            "projectdescription" or "project_description" or "project-description" or "pdesc" => ProjectDescriptionAsync(userId, chatId, cmd, ct),
            "blankcolor" or "blank_color" or "blank-color" or "bcolor" => BlankColorAsync(userId, chatId, cmd, ct),
            "expire" or "expiry" or "expiration" => ExpireAsync(userId, chatId, cmd, ct),
            "delete" or "remove" or "deleteproject" or "delete_project" => DeleteProjectAsync(userId, chatId, cmd, ct),
            "blank" => BlankAsync(userId, chatId, cmd, ct),
            "format" => FormatAsync(userId, chatId, cmd, ct),
            "url" => UrlAsync(userId, chatId, cmd, ct),
            "sourcesite" or "source_site" or "source-site" or "scrape" => SourceSiteAsync(userId, chatId, cmd, ct),
            "sourceupload" or "source_upload" or "source-upload" => SourceUploadAsync(userId, chatId, cmd, ct),
            "frame" => FrameAsync(userId, chatId, cmd, ct),
            "crop" => CropAsync(userId, chatId, cmd, ct),
            "rotate" => RotateAsync(userId, chatId, cmd, ct),
            "filter" => FilterAsync(userId, chatId, cmd, ct),
            "draw" => DrawAsync(userId, chatId, cmd, ct),
            "line" or "polyline" => DrawShapeAsync(userId, chatId, "line", cmd.Args, ct),
            "rect" or "rectangle" => DrawShapeAsync(userId, chatId, "rect", cmd.Args, ct),
            "poly" or "polygon" => DrawShapeAsync(userId, chatId, "poly", cmd.Args, ct),
            "color" or "colour" => PenColorAsync(userId, chatId, cmd, ct),
            "thickness" => PenThicknessAsync(userId, chatId, cmd, ct),
            "points" or "point" => PenPointsAsync(userId, chatId, cmd, ct),
            "style" => PenStyleAsync(userId, chatId, cmd, ct),
            "fill" => PenFillAsync(userId, chatId, cmd, ct),
            "pen" => PenAsync(userId, chatId, ct),
            "undo" => UndoAsync(userId, chatId, ct),
            "redo" => RedoAsync(userId, chatId, ct),
            "undoline" or "undo_line" => UndoLineAsync(userId, chatId, ct),
            "clearlines" or "clear_lines" => ClearLinesAsync(userId, chatId, ct),
            "reset" => ResetAsync(userId, chatId, ct),
            "drop" => DropAsync(userId, chatId, ct),
            "image" => ImageAsync(userId, chatId, ct),
            "layout" => LayoutAsync(userId, chatId, cmd, ct),
            "json" => JsonAsync(userId, chatId, ct),
            "project" => ProjectAsync(userId, chatId, ct),
            "status" => StatusAsync(userId, chatId, ct),
            "cancel" => CancelAsync(chatId, ct),
            _ => UnknownAsync(chatId, ct),
        };

    /// <summary>
    /// The shared send-photo tail: stream a rendered result file from disk as a "result.png"
    /// photo with its caption (and optional keyboard). Used by <see cref="RenderAndSendAsync"/>,
    /// <see cref="SendPromptRenderAsync"/> and the album fallback.
    /// </summary>
    private async Task SendResultPhotoAsync(long chatId, string path, string caption,
        InlineKeyboardMarkup? keyboard, CancellationToken ct)
    {
        await using FileStream stream = File.OpenRead(path);
        InputFileStream photo = InputFile.FromStream(stream, "result.png");
        await _bot.SendPhoto(
            chatId,
            photo,
            caption: caption,
            replyMarkup: keyboard,
            cancellationToken: ct);
    }

    /// <summary>Show the full command help plus the main menu.</summary>
    private Task HelpAsync(long chatId, CancellationToken ct) =>
        _bot.SendMessage(chatId, Replies.HelpText(), replyMarkup: Keyboards.MainMenu(), cancellationToken: ct);

    /// <summary>Export and send the layout JSON as a document.</summary>
    private async Task JsonAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (!session.HasImage)
        {
            await _bot.SendMessage(
                chatId,
                "No working image — upload a photo or use /blank first.",
                cancellationToken: ct);
            return;
        }
        string json = _editing.ExportLayoutJson(session);
        byte[] bytes = Encoding.UTF8.GetBytes(json);
        string fileName = $"{SafeLabel(session.ImageLabel)}.json";
        using MemoryStream stream = new(bytes);
        InputFileStream document = InputFile.FromStream(stream, fileName);
        await _bot.SendDocument(chatId, document, caption: "Layout JSON", cancellationToken: ct);
    }

    /// <summary>Export and send the whole project as a portable <c>.stencil</c> document.</summary>
    private async Task ProjectAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (!session.HasImage)
        {
            await _bot.SendMessage(
                chatId,
                "No working image — upload a photo or use /blank first.",
                cancellationToken: ct);
            return;
        }
        byte[] bytes = await _editing.ExportProjectFileAsync(userId, ct);
        string fileName = $"{SafeLabel(session.ImageLabel)}.stencil";
        using MemoryStream stream = new(bytes);
        InputFileStream document = InputFile.FromStream(stream, fileName);
        await _bot.SendDocument(chatId, document, caption: "Stencil project", cancellationToken: ct);
    }

    private async Task StatusAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        await _bot.SendMessage(
            chatId,
            Replies.StatusText(session),
            replyMarkup: Keyboards.StatusMenu(session.ActiveProjectId is not null),
            cancellationToken: ct);
    }

    /// <summary>A friendly no-op acknowledgement.</summary>
    private Task CancelAsync(long chatId, CancellationToken ct) =>
        _bot.SendMessage(chatId, "Okay, never mind. Send /help for the command list.", cancellationToken: ct);

    /// <summary>Unknown command — point at /help.</summary>
    private Task UnknownAsync(long chatId, CancellationToken ct) =>
        _bot.SendMessage(chatId, "Unknown command. Send /help for the list.", cancellationToken: ct);

    /// <summary>
    /// Replay the current edit state through the CLI and send the rendered result as a photo
    /// with the edit menu. Shared by the mutating commands and by <see cref="UpdateRouter"/>
    /// after a fresh upload or layout apply.
    /// </summary>
    public async Task RenderAndSendAsync(long userId, long chatId, CancellationToken ct, bool mutating = true)
    {
        // Album batch: buffer the render for the single media-group reply instead of sending.
        // (A fresh album adoption cleared any active project, so the auto-sync tail is moot.)
        if (_renderCaptures.TryGetValue(userId, out List<PromptRender>? captured))
        {
            RenderResult buffered = await _editing.RenderAsync(userId, ct);
            UserSession current = await _store.GetAsync(userId, ct);
            captured.Add(new PromptRender(current.ImageLabel ?? "image", buffered));
            return;
        }
        await _bot.SendChatAction(chatId, ChatAction.UploadPhoto, cancellationToken: ct);
        RenderResult result = await _editing.RenderAsync(userId, ct);
        UserSession session = await _store.GetAsync(userId, ct);
        await SendResultPhotoAsync(chatId, result.Path, BuildCaption(session, result),
            Keyboards.EditMenu(session.ActiveProjectId is not null), ct);
        // Live sync: a mutating edit on a synced active project auto-uploads so peers see it.
        if (mutating && session.SyncEnabled && session.ActiveProjectId is not null)
        {
            await AutoSyncAsync(userId, chatId, ct);
        }
    }

    /// <summary>Push the current result to the active project (best-effort), surfacing a conflict.</summary>
    private async Task AutoSyncAsync(long userId, long chatId, CancellationToken ct)
    {
        try
        {
            ProjectRecord record = await _servers.SaveActiveProjectAsync(userId, ct);
            // The ↑ already marks it; Tag keeps the line to one glyph.
            await _bot.SendMessage(
                chatId,
                Replies.Tag(Replies.Tone.Success, $"↑ synced to '{record.Name}' (v{record.Version})."),
                cancellationToken: ct);
        }
        catch (ServerException ex)
        {
            await _bot.SendMessage(
                chatId, Replies.Tag(Replies.Tone.Error, $"Couldn't sync: {ex.Message}"), cancellationToken: ct);
        }
    }

    /// <summary>A short caption: label and rendered size.</summary>
    private static string BuildCaption(UserSession session, RenderResult result)
    {
        string label = session.ImageLabel ?? "image";
        string caption = $"{label} — {result.Size}";
        // Show where the image came from (Telegram auto-links the URL, so it's tappable).
        if (session.SourceUrl is string src)
        {
            caption += $"\nSource: {src}";
        }
        return caption;
    }

    /// <summary>A short human label for a URL source: its file name, else its host.</summary>
    private static string LabelFromUrl(string url)
    {
        if (Uri.TryCreate(url, UriKind.Absolute, out Uri? uri))
        {
            string name = Path.GetFileName(uri.LocalPath);
            return name.Length > 0 ? name : uri.Host;
        }
        return "image";
    }

    /// <summary>A filesystem-safe stem for the JSON download (defaults to "layout").</summary>
    private static string SafeLabel(string? label)
    {
        if (string.IsNullOrWhiteSpace(label))
        {
            return "layout";
        }
        StringBuilder sb = new();
        foreach (char c in label)
        {
            sb.Append(char.IsLetterOrDigit(c) || c is '-' or '_' ? c : '_');
        }
        string cleaned = sb.ToString().Trim('_');
        return cleaned.Length == 0 ? "layout" : cleaned;
    }
}
