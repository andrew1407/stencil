using System.Text;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Project;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Bot.Telegram;

// A .json layout overlays the working image, a .stencil opens as a project, a .stc runs as a
// script, an image/video document is adopted.
public sealed class DocumentIntake
{
    private readonly MediaIntake _media;
    private readonly CommandHandlers _handlers;
    private readonly IEditingService _editing;
    private readonly ISessionStore _store;
    private readonly ITelegramBotClient _bot;

    public DocumentIntake(
        MediaIntake media, CommandHandlers handlers, IEditingService editing,
        ISessionStore store, ITelegramBotClient bot)
    {
        _media = media;
        _handlers = handlers;
        _editing = editing;
        _store = store;
        _bot = bot;
    }

    public async Task HandleAsync(long userId, long chatId, Document document, string? caption, CancellationToken ct)
    {
        string name = document.FileName ?? "";
        bool isJson = name.EndsWith(".json", StringComparison.OrdinalIgnoreCase)
            || string.Equals(document.MimeType, "application/json", StringComparison.OrdinalIgnoreCase);
        if (isJson)
        {
            await applyLayoutDocumentAsync(userId, chatId, document.FileId, caption, ct);
            return;
        }
        if (name.EndsWith(".stencil", StringComparison.OrdinalIgnoreCase))
        {
            await openProjectDocumentAsync(userId, chatId, document.FileId, ct);
            return;
        }
        if (name.EndsWith(".stc", StringComparison.OrdinalIgnoreCase))
        {
            await runScriptDocumentAsync(userId, chatId, document.FileId, ct);
            return;
        }
        if (DocumentKinds.IsImage(document))
        {
            string ext = DocumentKinds.ExtensionOf(name, ".png");
            string label = name.Length == 0 ? "image" : name;
            await _media.AdoptImageAsync(userId, chatId, document.FileId, ext, label, caption, ct);
            return;
        }
        if (DocumentKinds.IsVideo(document))
        {
            string ext = DocumentKinds.ExtensionOf(name, ".mp4");
            string label = name.Length == 0 ? "video" : name;
            await _media.AdoptVideoAsync(userId, chatId, document.FileId, ext, label, caption, ct);
            return;
        }
        await _bot.SendMessage(
            chatId,
            "Unsupported file. Send an image or video to edit, a .stc script to run, a .stencil project "
            + "to open, or a .json layout with caption /apply.",
            cancellationToken: ct);
    }

    private async Task applyLayoutDocumentAsync(long userId, long chatId, string fileId, string? caption, CancellationToken ct)
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
        byte[] bytes = await _media.DownloadDocumentBytesAsync(fileId, ".json", ct);
        StencilLayout? layout = StencilLayoutParser.Parse(bytes);
        if (layout is null)
        {
            await _bot.SendMessage(chatId, "That file isn't a valid Stencil layout JSON.", cancellationToken: ct);
            return;
        }
        await _editing.ApplyLayoutAsync(userId, layout, ct: ct);
        await _handlers.RenderAndSendAsync(userId, chatId, ct);
    }

    // The file form of /script; the download already rides IBotPolicy.MaxDocumentBytes.
    private async Task runScriptDocumentAsync(long userId, long chatId, string fileId, CancellationToken ct)
    {
        byte[] bytes = await _media.DownloadDocumentBytesAsync(fileId, ".stc", ct);
        if (bytes.Length > ScriptService.MAX_SCRIPT_CHARS)
        {
            await _bot.SendMessage(chatId, "That .stc file is too large to run.", cancellationToken: ct);
            return;
        }
        // A BOM would otherwise reach the lexer as a stray token on line 1.
        string text = new UTF8Encoding(false).GetString(bytes).TrimStart('\uFEFF');
        await _handlers.RunScriptAsync(userId, chatId, text, ct);
    }

    private async Task openProjectDocumentAsync(long userId, long chatId, string fileId, CancellationToken ct)
    {
        byte[] bytes = await _media.DownloadDocumentBytesAsync(fileId, ".stencil", ct);
        StencilProject? project = StencilProjectFile.Parse(bytes);
        if (project is null)
        {
            await _bot.SendMessage(chatId, "That file isn't a valid .stencil project.", cancellationToken: ct);
            return;
        }
        await _editing.OpenProjectFileAsync(userId, project, ct);
        await _handlers.RenderAndSendAsync(userId, chatId, ct);
    }
}
