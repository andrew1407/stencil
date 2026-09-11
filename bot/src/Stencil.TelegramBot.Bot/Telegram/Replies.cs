using System.Buffers;
using System.Globalization;
using System.Text;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Static plain-text builders for the bot's chat replies. Kept free of Telegram types so the
/// wording is unit-testable. Mirrors the help/status surface every other front-end exposes
/// (the browser console help registry, the CLI <c>--help</c>, pystencil's prompts).
/// </summary>
public static partial class Replies
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

    private static bool OpensWithGlyph(string text) =>
        Rune.DecodeFromUtf16(text, out Rune first, out _) == OperationStatus.Done
        && Rune.GetUnicodeCategory(first) is UnicodeCategory.OtherSymbol or UnicodeCategory.MathSymbol
            or UnicodeCategory.ModifierSymbol or UnicodeCategory.CurrencySymbol;

    public static string HelpText() => BotCommands.HelpText;

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
}
