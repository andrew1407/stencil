using System.Globalization;
using System.Text;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Bot.Telegram;

// Replies — what the working image currently is: the pen, the pending edits, page formats and
// the per-project prompts that precede a destructive step. Class doc lives in Replies.cs.
public static partial class Replies
{
    /// <summary>
    /// The bare <c>/expire</c> / Expiration-button header: the active project's current expiry
    /// (or "no expiry") plus a "choose one" line above the duration picker.
    /// </summary>
    public static string ExpiryPrompt(long expiresAtMs)
    {
        string current = expiresAtMs > 0
            ? BotStrings.Reply("expiryCurrent", FmtDate(expiresAtMs))
            : BotStrings.Reply("expiryNone");
        return BotStrings.Reply("expiryChoose", current);
    }

    public static string ExpireUsage() => BotStrings.Reply("expireUsage");

    public static string DeleteConfirmPrompt(string name, string? serverUrl)
    {
        string where = serverUrl is null ? "" : BotStrings.Reply("deleteConfirmWhere", Host(serverUrl));
        return BotStrings.Reply("deleteConfirm", name, where);
    }

    /// <summary>
    /// The <c>/link</c> reply: the desktop hand-off link plus what it does and doesn't carry. A
    /// loopback link resolves on whoever taps it, so say so rather than let it look shareable.
    /// </summary>
    public static string DesktopLink(string name, string url, bool loopback) =>
        BotStrings.Reply("desktopLinkHead", name, url)
        + (loopback ? BotStrings.Reply("desktopLinkLoopback", Glyph(Tone.Notice)) : "");

    public static string DesktopLinkUnusable() => Tag(Tone.Notice, BotStrings.Reply("desktopLinkUnusable"));

    /// <summary>
    /// All named page formats with their portrait cm sizes (canonical order), plus the custom
    /// variant — the bare <c>/format</c> reply.
    /// </summary>
    public static string PageFormatList()
    {
        StringBuilder sb = new();
        sb.AppendLine(BotStrings.Reply("pageFormatsHeader"));
        foreach (var (name, w, h) in PageFormats.All)
        {
            sb.AppendLine(BotStrings.Reply("pageFormatsEntry", name, PageFormats.Cm(w), PageFormats.Cm(h)));
        }
        sb.AppendLine();
        sb.AppendLine(BotStrings.Reply("pageFormatsCustom"));
        sb.Append(BotStrings.Reply("pageFormatsFooter"));
        return sb.ToString();
    }

    public static string PenText(LineStyle pen)
    {
        StringBuilder sb = new();
        sb.AppendLine(BotStrings.Reply("penHeader"));
        sb.AppendLine(BotStrings.Reply("penColor", pen.Color));
        sb.AppendLine(BotStrings.Reply("penThickness", pen.Thickness));
        sb.AppendLine(BotStrings.Reply("penPoints", pen.PointSize));
        sb.AppendLine(BotStrings.Reply("penStyle", pen.Style));
        sb.Append(BotStrings.Reply("penFill", pen.FillColor));
        return sb.ToString();
    }

    private static string PenSummary(LineStyle pen) =>
        BotStrings.Reply("penSummary", pen.Color, pen.Thickness, pen.Style, pen.PointSize, pen.FillColor);

    public static string DescribeEdits(EditState edits)
    {
        if (edits.IsEmpty)
        {
            return "none";
        }
        List<string> parts = new();
        if (edits.CropSpec is not null)
        {
            parts.Add(edits.Album ? $"crop[{edits.CropSpec}] album" : $"crop[{edits.CropSpec}]");
        }
        if (edits.Rotate != 0)
        {
            parts.Add($"rotate {edits.Rotate * 90}°");
        }
        if (edits.Filter is not null)
        {
            parts.Add($"filter {edits.Filter}");
        }
        if (edits.PageFormat is not null)
        {
            parts.Add(edits.PageFormat == "custom" && edits.CustomPageWidth is double pw && edits.CustomPageHeight is double ph
                ? $"page custom {PageFormats.Cm(pw)}×{PageFormats.Cm(ph)}cm"
                : $"page {edits.PageFormat}");
        }
        if (edits.Layout is not null)
        {
            int lines = edits.Layout.Lines.Count;
            parts.Add($"layout ({lines} line{(lines == 1 ? "" : "s")})");
        }
        return string.Join(", ", parts);
    }

}
