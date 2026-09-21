using System.Text;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Configuration;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Links;
using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.Enums;
using Telegram.Bot.Types.ReplyMarkups;
using Stencil.TelegramBot.Bot.Telegram.Access;
using Stencil.TelegramBot.Bot.Telegram.Sync;
using Stencil.TelegramBot.Bot.Telegram.Messaging;

namespace Stencil.TelegramBot.Bot.Telegram.Commands;

// Each command folds through the same Application services the callback buttons use, then replies
// over Telegram.
public sealed partial class CommandHandlers
{
    private readonly IEditingService _editing;
    private readonly IServerService _servers;
    private readonly ISessionStore _store;
    private readonly ITelegramBotClient _bot;
    private readonly IBotPolicy _options;
    private readonly SyncRegistry _sync;
    private readonly LayoutFetcher _layoutFetcher;
    private readonly PromptService _prompts;
    private readonly IScriptService _script;
    private readonly LlmAttachmentLoader _attachments;
    private readonly PromptCancellations _cancellations;
    private readonly ILogger<CommandHandlers> _logger;

    public CommandHandlers(
        IEditingService editing,
        IServerService servers,
        ISessionStore store,
        ITelegramBotClient bot,
        IBotPolicy options,
        SyncRegistry sync,
        LayoutFetcher layoutFetcher,
        PromptService prompts,
        IScriptService script,
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
        _script = script;
        _attachments = attachments;
        _cancellations = cancellations;
        _logger = logger;
    }

    // Streams the result file as a "result.png" photo; shared by the render, prompt-render and
    // album paths.
    private async Task sendResultPhotoAsync(long chatId, string path, string caption,
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

    private Task helpAsync(long chatId, CancellationToken ct) =>
        _bot.SendMessage(chatId, Replies.HelpText(), replyMarkup: Keyboards.MainMenu(), cancellationToken: ct);

    private async Task jsonAsync(long userId, long chatId, CancellationToken ct)
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
        string fileName = $"{safeLabel(session.ImageLabel)}.json";
        using MemoryStream stream = new(bytes);
        InputFileStream document = InputFile.FromStream(stream, fileName);
        await _bot.SendDocument(chatId, document, caption: "Layout JSON", cancellationToken: ct);
    }

    private async Task projectAsync(long userId, long chatId, CancellationToken ct)
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
        string fileName = $"{safeLabel(session.ImageLabel)}.stencil";
        using MemoryStream stream = new(bytes);
        InputFileStream document = InputFile.FromStream(stream, fileName);
        await _bot.SendDocument(chatId, document, caption: "Stencil project", cancellationToken: ct);
    }

    private async Task statusAsync(long userId, long chatId, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        await _bot.SendMessage(
            chatId,
            Replies.StatusText(session),
            replyMarkup: Keyboards.StatusMenu(session.ActiveProjectId is not null),
            cancellationToken: ct);
    }

    private Task cancelAsync(long chatId, CancellationToken ct) =>
        _bot.SendMessage(chatId, "Okay, never mind. Send /help for the command list.", cancellationToken: ct);

    private Task unknownAsync(long chatId, CancellationToken ct) =>
        _bot.SendMessage(chatId, "Unknown command. Send /help for the list.", cancellationToken: ct);

    // Shared by the mutating commands and by UpdateRouter after a fresh upload or layout apply.
    public async Task RenderAndSendAsync(long userId, long chatId, CancellationToken ct, bool mutating = true)
    {
        // Album batch: buffer the render for the single media-group reply instead of sending.
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
        await sendResultPhotoAsync(chatId, result.Path, buildCaption(session, result),
            Keyboards.EditMenu(session.ActiveProjectId is not null), ct);
        // Live sync: a mutating edit on a synced active project auto-uploads so peers see it.
        if (mutating && session.SyncEnabled && session.ActiveProjectId is not null)
        {
            await autoSyncAsync(userId, chatId, ct);
        }
    }

    private async Task autoSyncAsync(long userId, long chatId, CancellationToken ct)
    {
        try
        {
            ProjectRecord record = await _servers.SaveActiveProjectAsync(userId, ct);
            // The ↑ already marks it; Tag keeps the line to one glyph.
            await _bot.SendMessage(
                chatId,
                Replies.Tag(Replies.Tone.SUCCESS, $"↑ synced to '{record.Name}' (v{record.Version})."),
                cancellationToken: ct);
        }
        catch (ServerException ex)
        {
            await _bot.SendMessage(
                chatId, Replies.Tag(Replies.Tone.ERROR, $"Couldn't sync: {ex.Message}"), cancellationToken: ct);
        }
    }

    private static string buildCaption(UserSession session, RenderResult result)
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

    private static string labelFromUrl(string url)
    {
        if (Uri.TryCreate(url, UriKind.Absolute, out Uri? uri))
        {
            string name = Path.GetFileName(uri.LocalPath);
            return name.Length > 0 ? name : uri.Host;
        }
        return "image";
    }

    private static string safeLabel(string? label)
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
