using System.Buffers;
using System.Globalization;
using System.Text;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Static plain-text builders for the bot's chat replies. Kept free of Telegram types so the
/// wording is unit-testable. Mirrors the help/status surface every other front-end exposes
/// (the browser console help registry, the CLI <c>--help</c>, pystencil's prompts).
/// </summary>
public static class Replies
{
    /// <summary>
    /// How a reply reads at a glance. Telegram has no icon assets, so the vocabulary is emoji —
    /// the same one the buttons and the chat affordances already speak (💾 / 🧹 / 🗑).
    /// </summary>
    public enum Tone
    {
        /// <summary>The request did not happen: an error, a failure, or a refusal.</summary>
        Error,

        /// <summary>It happened, but partially or with a caveat worth reading.</summary>
        Warning,

        /// <summary>A confirmed action.</summary>
        Success,

        /// <summary>A plain notice — nothing failed, nothing changed.</summary>
        Notice,
    }

    /// <summary>The glyph a tone wears — the one place the convention is defined.</summary>
    public static string Glyph(Tone tone) => BotStrings.Tone(tone.ToString());

    /// <summary>
    /// Prefix a reply with its tone glyph. A message that already opens with one of its own
    /// (the 🗑 delete confirmation, the ↑ sync line) keeps it: one glyph per message, never two.
    /// </summary>
    public static string Tag(Tone tone, string message) =>
        OpensWithGlyph(message) ? message : $"{Glyph(tone)} {message}";

    /// <summary>True when the text already starts with a symbol rune (emoji, arrow, …).</summary>
    private static bool OpensWithGlyph(string text) =>
        Rune.DecodeFromUtf16(text, out Rune first, out _) == OperationStatus.Done
        && Rune.GetUnicodeCategory(first) is UnicodeCategory.OtherSymbol or UnicodeCategory.MathSymbol
            or UnicodeCategory.ModifierSymbol or UnicodeCategory.CurrencySymbol;

    /// <summary>The full slash-command reference, noting that the inline buttons mirror them.</summary>
    public static string HelpText() => BotCommands.HelpText;

    /// <summary>A short status block: working image label/size, pending edits, active project.</summary>
    public static string StatusText(UserSession session)
    {
        StringBuilder sb = new();
        if (session.HasImage)
        {
            ImageSize size = new(session.OriginalWidth, session.OriginalHeight);
            string label = session.ImageLabel ?? "image";
            sb.AppendLine(BotStrings.Reply("statusImage", label, size));
            if (session.SourceUrl is string src)
            {
                sb.AppendLine(BotStrings.Reply("statusSource", src));
            }
            // The description belongs to the working image whether or not it's saved to a server yet
            // (set via /project-description; carried into /create), so show it here in both cases.
            if (!string.IsNullOrEmpty(session.ActiveProjectDescription))
            {
                sb.AppendLine(BotStrings.Reply("statusDescription", session.ActiveProjectDescription));
            }
        }
        else
        {
            sb.AppendLine(BotStrings.Reply("statusNoImage"));
        }
        sb.AppendLine(BotStrings.Reply("statusEdits", DescribeEdits(session.Edits)));
        if (session.ActiveProjectId is not null)
        {
            string name = session.ActiveProjectName ?? session.ActiveProjectId;
            sb.AppendLine(BotStrings.Reply("statusProject", name, session.ActiveServerUrl, session.ActiveProjectVersion));
            string created = FmtDate(session.ActiveProjectCreatedAt);
            if (created.Length != 0)
            {
                sb.AppendLine(BotStrings.Reply("statusCreated", created));
            }
            string expires = FmtDate(session.ActiveProjectExpiresAt);
            if (expires.Length != 0)
            {
                sb.AppendLine(BotStrings.Reply("statusExpires", expires));
            }
        }
        else
        {
            sb.AppendLine(BotStrings.Reply("statusNoProject"));
        }
        if (session.VideoSourcePath is not null)
        {
            sb.AppendLine(BotStrings.Reply("statusVideo"));
        }
        sb.AppendLine(BotStrings.Reply("statusPen", PenSummary(session.Edits.Pen)));
        if (session.ChatMode)
        {
            sb.AppendLine(BotStrings.Reply("statusChatMode"));
        }
        if (session.SaveChats)
        {
            sb.AppendLine(BotStrings.Reply("statusChatSaving"));
        }
        sb.Append(BotStrings.Reply("statusConnections", session.Connections.Count));
        return sb.ToString();
    }

    /// <summary>Confirmation shown when chat mode is switched on (rides the "Chat off" button).</summary>
    public static string ChatModeOn() => BotStrings.Reply("chatModeOn");

    /// <summary>Confirmation shown when chat mode is switched off.</summary>
    public static string ChatModeOff() => BotStrings.Reply("chatModeOff");

    /// <summary>
    /// Confirmation for <c>/chat clear</c> — the assistant's conversation is forgotten; chat mode
    /// itself is untouched, which the reply spells out when it is on.
    /// </summary>
    public static string ChatHistoryCleared(bool chatModeOn) =>
        BotStrings.Reply(chatModeOn ? "chatClearedModeOn" : "chatCleared");

    /// <summary>
    /// What the spinning <see cref="ProgressNotice"/> says while an assistant turn runs — the
    /// model call, the edits it plans and every render it asks for.
    /// </summary>
    public static string PromptWorking() => BotStrings.Reply("promptWorking");

    /// <summary>
    /// A turn ended by the ⏹ Stop button: asked for and delivered, so a notice, never an error.
    /// Ops already applied stay (/undo walks them back) and a 🔄 Retry button rides along.
    /// </summary>
    public static string PromptStopped() => Tag(Tone.Notice, BotStrings.Reply("promptStopped"));

    /// <summary>Acknowledges the ⏹ tap; the turn's own "Stopped." lands when it unwinds.</summary>
    public static string PromptStopping() => Tag(Tone.Notice, BotStrings.Reply("promptStopping"));

    /// <summary>/chatapi on a bot whose operator configured no alternatives.</summary>
    public static string ChatApiNoProfiles() => Tag(Tone.Notice, BotStrings.Reply("chatApiNoProfiles"));

    /// <summary>The picker's text: every configured API, with the caller's current one marked.</summary>
    public static string ChatApiList(IReadOnlyList<LlmProfile> profiles, LlmProfile? current)
    {
        StringBuilder sb = new();
        sb.Append(BotStrings.Reply("chatApiListHeader"));
        foreach (LlmProfile p in profiles)
        {
            sb.Append(BotStrings.Reply(p.Name == current?.Name ? "chatApiListCurrentPrefix" : "chatApiListOtherPrefix"));
            sb.Append(p.Label).Append(BotStrings.Reply("chatApiListSeparator")).Append(p.Summary());
        }
        sb.Append(BotStrings.Reply("chatApiListNowUsing")).Append(current?.Label ?? BotStrings.Reply("chatApiListDefault"));
        return sb.ToString();
    }

    /// <summary>Confirmation after a pick.</summary>
    public static string ChatApiSelected(LlmProfile picked) =>
        Tag(Tone.Success, BotStrings.Reply("chatApiSelected", picked.Label, picked.Summary()));

    /// <summary>A name that is not configured — with the ones that are.</summary>
    public static string ChatApiUnknown(string wanted, IReadOnlyList<LlmProfile> profiles) => Tag(
        Tone.Error, BotStrings.Reply("chatApiUnknown", wanted, string.Join(", ", profiles.Select(p => p.Name))));

    /// <summary>
    /// The §10 <c>clearChat</c> in-app confirmation, sent at the END of the plan's turn — the
    /// model can ask, but only the user's Yes button clears anything.
    /// </summary>
    public static string ClearChatConfirm() => BotStrings.Reply("clearChatConfirm");

    /// <summary>The declined <c>clearChat</c> confirm — a note, never a failed plan (§10).</summary>
    public static string ClearChatCanceled() => BotStrings.Reply("clearChatCanceled");

    /// <summary>Usage hint for <c>/chat</c> with an unrecognised argument.</summary>
    public static string ChatUsage() => BotStrings.Reply("chatUsage");

    /// <summary>Confirmation for <c>/chat save on</c> (contract §12.3 — the server project is the store).</summary>
    public static string ChatSaveOn() => BotStrings.Reply("chatSaveOn");

    /// <summary>Confirmation for <c>/chat save off</c> (§12.2: no retroactive delete).</summary>
    public static string ChatSaveOff() => BotStrings.Reply("chatSaveOff");

    /// <summary>The current chat-saving setting, for a bare <c>/chat save</c>.</summary>
    public static string ChatSaveStatus(bool on) =>
        BotStrings.Reply(on ? "chatSaveStatusOn" : "chatSaveStatusOff");

    /// <summary>The fetch-reply line for a restored persisted chat (only shown when N &gt; 0).</summary>
    public static string ChatRestored(int count) =>
        BotStrings.Reply(count == 1 ? "chatRestoredOne" : "chatRestoredMany", count);

    /// <summary>The once-per-streak warning when the best-effort chat save-back fails (§12).</summary>
    public static string ChatSaveFailed() => Tag(Tone.Warning, BotStrings.Reply("chatSaveFailed"));

    /// <summary>Usage hint for adding a working image from a link or web page (the Sources button).</summary>
    public static string SourcesHelp() => BotStrings.Reply("sourcesHelp");

    /// <summary>Usage hint for the <c>/draw</c> family.</summary>
    public static string DrawHelp() => BotStrings.Reply("drawHelp");

    /// <summary>The filter variants for a bare <c>/filter</c> (mirrors the CLI console's list).</summary>
    public static string FilterVariants() => BotStrings.Reply("filterVariants");

    /// <summary>The quarter-turn variants for a bare <c>/rotate</c>.</summary>
    public static string RotateVariants() => BotStrings.Reply("rotateVariants");

    /// <summary>The crop-spec vocabulary for a bare <c>/crop</c> (and the Crop… button).</summary>
    public static string CropUsage() => BotStrings.Reply("cropUsage");

    /// <summary>Usage hint for <c>/connect</c> (a bare command and the Connect… button).</summary>
    public static string ConnectUsage() => BotStrings.Reply("connectUsage");

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

    /// <summary>Usage hint for <c>/expire</c> (an unparseable duration argument).</summary>
    public static string ExpireUsage() => BotStrings.Reply("expireUsage");

    /// <summary>The delete-project confirmation question (bare <c>/delete</c> and the 🗑 Remove button).</summary>
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

    /// <summary>The <c>/link</c> reply when the configured browser app isn't a usable address.</summary>
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

    /// <summary>A full description of the current pen.</summary>
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

    /// <summary>A one-line pen summary for the status block.</summary>
    private static string PenSummary(LineStyle pen) =>
        BotStrings.Reply("penSummary", pen.Color, pen.Thickness, pen.Style, pen.PointSize, pen.FillColor);

    /// <summary>One-line human summary of the pending <see cref="EditState"/>.</summary>
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

    /// <summary>Usage hint for <c>/connections</c> with an unrecognised filter argument.</summary>
    public static string ConnectionsUsage() => BotStrings.Reply("connectionsUsage");

    /// <summary>
    /// The remembered connections (or a hint when none), narrowed by an <c>admin</c>/<c>session</c>
    /// <paramref name="filter"/>. Admin connections are marked — never the token itself.
    /// </summary>
    public static string ConnectionsText(IReadOnlyList<ServerConnectionInfo> connections, string filter = "")
    {
        if (connections.Count == 0)
        {
            return filter.Length == 0
                ? BotStrings.Reply("connectionsEmpty")
                : BotStrings.Reply("connectionsEmptyFiltered", filter);
        }
        string header = filter switch
        {
            "admin" => BotStrings.Reply("connectionsHeaderAdmin", connections.Count),
            "session" => BotStrings.Reply("connectionsHeaderSession", connections.Count),
            _ => BotStrings.Reply("connectionsHeader", connections.Count),
        };
        StringBuilder sb = new();
        sb.AppendLine(header);
        for (int i = 0; i < connections.Count; i++)
        {
            ServerConnectionInfo c = connections[i];
            string tls = c.VerifyTls ? "" : BotStrings.Reply("connectionsTlsOff");
            string kind = c.CredentialKind == CredentialKind.Admin ? BotStrings.Reply("connectionsAdminMark") : "";
            sb.AppendLine(BotStrings.Reply("connectionsEntry", i + 1, c.Url, kind, tls));
        }
        return sb.ToString().TrimEnd();
    }

    /// <summary>
    /// The most projects one list message / keyboard renders — Telegram caps both. The overflow
    /// is called out, never silently dropped (narrow with <c>/projects &lt;url&gt;</c>).
    /// </summary>
    public const int MaxProjectsListed = 20;

    /// <summary>List the aggregated cross-server projects (or a hint when none), capped.</summary>
    public static string ProjectsText(IReadOnlyList<ServerProjectInfo> projects)
    {
        if (projects.Count == 0)
        {
            return BotStrings.Reply("projectsEmpty");
        }
        int shown = Math.Min(projects.Count, MaxProjectsListed);
        StringBuilder sb = new();
        sb.AppendLine(BotStrings.Reply("projectsHeader", projects.Count));
        for (int i = 0; i < shown; i++)
        {
            ServerProjectInfo p = projects[i];
            string size = p.Record.HasImage ? BotStrings.Reply("projectsSize", p.Record.ImageW, p.Record.ImageH) : "";
            string dot = ColorDot(p.Record.Color);
            string prefix = dot.Length == 0 ? "•" : dot;
            string created = FmtDate(p.Record.CreatedAt);
            string createdBit = created.Length == 0 ? "" : BotStrings.Reply("projectsCreated", created);
            string expires = FmtDate(p.Record.ExpiresAt);
            string expiresBit = expires.Length == 0 ? "" : BotStrings.Reply("projectsExpires", expires);
            sb.AppendLine(BotStrings.Reply("projectsEntry", prefix, p.Record.Name, size, createdBit, expiresBit, Host(p.ServerUrl)));
            if (!string.IsNullOrEmpty(p.Record.Description))
            {
                sb.AppendLine(BotStrings.Reply("projectsDescription", p.Record.Description));
            }
        }
        if (projects.Count > shown)
        {
            sb.AppendLine(BotStrings.Reply("projectsOverflow", projects.Count - shown));
        }
        return sb.ToString().TrimEnd();
    }

    /// <summary>
    /// A coloured-circle emoji for a project's accent (Telegram can't tint text): a hex maps to
    /// the nearest palette dot, a CSS name falls back to 🎨, empty yields "".
    /// </summary>
    public static string ColorDot(string? color)
    {
        if (string.IsNullOrWhiteSpace(color))
        {
            return "";
        }
        if (!TryParseHex(color, out int r, out int g, out int b))
        {
            return "🎨"; // a named colour we can't cheaply resolve — still signals "has a colour"
        }
        (int R, int G, int B, string Dot)[] palette =
        [
            (220, 50, 50, "🔴"), (240, 150, 30, "🟠"), (245, 220, 60, "🟡"),
            (60, 180, 75, "🟢"), (60, 120, 220, "🔵"), (150, 80, 200, "🟣"),
            (140, 90, 60, "🟤"), (30, 30, 30, "⚫"), (240, 240, 240, "⚪"),
        ];
        string best = "🎨";
        long bestDist = long.MaxValue;
        foreach (var c in palette)
        {
            long d = (long)(c.R - r) * (c.R - r) + (long)(c.G - g) * (c.G - g) + (long)(c.B - b) * (c.B - b);
            if (d < bestDist)
            {
                bestDist = d;
                best = c.Dot;
            }
        }
        return best;
    }

    private static bool TryParseHex(string color, out int r, out int g, out int b)
    {
        r = g = b = 0;
        string s = color.Trim().TrimStart('#');
        if (s.Length == 3)
        {
            s = string.Concat(s[0], s[0], s[1], s[1], s[2], s[2]);
        }
        if (s.Length != 6)
        {
            return false;
        }
        return int.TryParse(s.AsSpan(0, 2), System.Globalization.NumberStyles.HexNumber, null, out r)
            && int.TryParse(s.AsSpan(2, 2), System.Globalization.NumberStyles.HexNumber, null, out g)
            && int.TryParse(s.AsSpan(4, 2), System.Globalization.NumberStyles.HexNumber, null, out b);
    }

    /// <summary>The host[:port] of a normalised origin, for compact labels.</summary>
    public static string Host(string url)
    {
        if (Uri.TryCreate(url, UriKind.Absolute, out Uri? uri))
        {
            return uri.Authority;
        }
        return url;
    }

    /// <summary>Format an epoch-ms timestamp as an ISO date (UTC), or "" when unset.</summary>
    public static string FmtDate(long ms) =>
        ms <= 0 ? "" : DateTimeOffset.FromUnixTimeMilliseconds(ms).ToString("yyyy-MM-dd");
}
