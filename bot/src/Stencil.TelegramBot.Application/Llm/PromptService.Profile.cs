using System.Text;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Llm;

// PromptService — the §10 bot-profile ops that act on the working image or send a document.
// Class doc lives in PromptService.cs.
public sealed partial class PromptService
{
    /// <summary>
    /// §10 <c>clear</c>, scoped to the IMAGE AND EDITS ONLY: the same image wipe <c>/drop</c>
    /// performs — but NEVER the <c>/drop</c> chat wipe. The assistant conversation survives
    /// (clearing IT is the separate, user-confirmed <c>clearChat</c> op).
    /// </summary>
    internal async Task ClearImageAsync(ActionContext ctx, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(ctx.UserId, ct);
        if (!session.HasImage)
        {
            ctx.Warnings.Add("Skipped clear — there is no working image to remove.");
            return;
        }
        await _editing.DropImageAsync(ctx.UserId, ct);
    }

    /// <summary>
    /// §10 <c>lineStyle</c>: the pen-default paths of <c>/color</c> <c>/thickness</c>
    /// <c>/points</c> <c>/style</c> <c>/fill</c>, in one call. The §10 fields the bot's pen
    /// doesn't model (pointColor, drawMode) are noted and skipped.
    /// </summary>
    internal async Task ConfigurePenAsync(ActionContext ctx, LineStyleAction pen, CancellationToken ct)
    {
        if (pen.PointColor is not null)
        {
            ctx.Warnings.Add("Skipped \"pointColor\" — the bot's pen has no separate point colour.");
        }
        if (pen.DrawMode is not null)
        {
            ctx.Warnings.Add("Skipped \"drawMode\" — the bot's pen has no draw-mode default.");
        }
        if (pen.Color is null && pen.Thickness is null && pen.PointSize is null
            && pen.Style is null && pen.FillColor is null)
        {
            return;   // only unsupported fields — nothing to configure
        }
        await _editing.ConfigurePenAsync(
            ctx.UserId, pen.Color, pen.Thickness, pen.PointSize, pen.Style, pen.FillColor, ct);
    }

    /// <summary>
    /// §10 <c>openUrl</c>: the <c>/url</c> load path with its SSRF vetting intact (the echo guard
    /// already passed). The load is AWAITED so later actions never race it; a vetting or fetch
    /// failure is a warning and the plan continues on the previous image.
    /// </summary>
    internal async Task OpenUrlAsync(ActionContext ctx, OpenUrlAction open, CancellationToken ct)
    {
        try
        {
            await _editing.SetImageFromUrlAsync(ctx.UserId, open.Url, LabelFromUrl(open.Url), ct);
            await ResetMapperAsync(ctx.UserId, ctx.Mapper, ct);
            if (open.Incognito)
            {
                ctx.Warnings.Add("Ignored \"incognito\" — a Telegram chat has no incognito mode.");
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            ctx.Warnings.Add($"Skipped opening {Shown(open.Url)} — {ex.Message}");
        }
    }

    /// <summary>
    /// §10 <c>export</c>: build the SAME document <c>/json</c> / <c>/project</c> send — the
    /// layout JSON or the portable <c>.stencil</c> bundle — for the caller to send into the
    /// user's own chat, bounded to one send per action. Misses are notes.
    /// </summary>
    internal async Task ExportAsync(ActionContext ctx, ExportAction export, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(ctx.UserId, ct);
        if (!session.HasImage)
        {
            ctx.Warnings.Add("Skipped export — there is no working image to export.");
            return;
        }
        try
        {
            if (export.What == "layout")
            {
                string json = _editing.ExportLayoutJson(session);
                ctx.Exports.Add(new PromptExport(
                    SafeLabel(session.ImageLabel) + ".json", Encoding.UTF8.GetBytes(json), "Layout JSON"));
            }
            else
            {
                byte[] bytes = await _editing.ExportProjectFileAsync(ctx.UserId, ct);
                ctx.Exports.Add(new PromptExport(
                    SafeLabel(session.ImageLabel) + ".stencil", bytes, "Stencil project"));
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            ctx.Warnings.Add($"Skipped export — {ex.Message}");
        }
    }

    /// <summary>A short human label for a URL source: its file name, else its host (the /url rule).</summary>
    private static string LabelFromUrl(string url)
    {
        if (Uri.TryCreate(url, UriKind.Absolute, out Uri? uri))
        {
            string name = Path.GetFileName(uri.LocalPath);
            return name.Length > 0 ? name : uri.Host;
        }
        return "image";
    }

    /// <summary>A filesystem-safe stem for an export file name (the /json rule; defaults to "layout").</summary>
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
