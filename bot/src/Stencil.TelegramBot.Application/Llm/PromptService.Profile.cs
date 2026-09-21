using System.Text;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Application.Llm.Plan;

namespace Stencil.TelegramBot.Application.Llm;

public sealed partial class PromptService
{
    // The IMAGE AND EDITS ONLY: the /drop image wipe, never its chat wipe (that is the confirmed
    // clearChat).
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

    // pointColor/drawMode are not modelled by the bot's pen: noted and skipped.
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

    // The /url path with its SSRF vetting; the load is AWAITED so later actions never race it; a
    // failure is a warning.
    internal async Task OpenUrlAsync(ActionContext ctx, OpenUrlAction open, CancellationToken ct)
    {
        try
        {
            await _editing.SetImageFromUrlAsync(ctx.UserId, open.Url, labelFromUrl(open.Url), ct);
            await resetMapperAsync(ctx.UserId, ctx.Mapper, ct);
            if (open.Incognito)
            {
                ctx.Warnings.Add("Ignored \"incognito\" — a Telegram chat has no incognito mode.");
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            ctx.Warnings.Add($"Skipped opening {showServer(open.Url)} — {ex.Message}");
        }
    }

    // The SAME document /json and /project send, one per action. Misses are notes.
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
                    safeLabel(session.ImageLabel) + ".json", Encoding.UTF8.GetBytes(json), "Layout JSON"));
            }
            else
            {
                byte[] bytes = await _editing.ExportProjectFileAsync(ctx.UserId, ct);
                ctx.Exports.Add(new PromptExport(
                    safeLabel(session.ImageLabel) + ".stencil", bytes, "Stencil project"));
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            ctx.Warnings.Add($"Skipped export — {ex.Message}");
        }
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
