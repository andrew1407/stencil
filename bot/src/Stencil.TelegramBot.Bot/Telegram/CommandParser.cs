namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// One parsed slash command: the lowercased verb, the raw trailing argument text (trimmed),
/// and that argument text tokenised on whitespace.
/// </summary>
/// <remarks>
/// Mirrors the way the other front-ends split a console line into a verb plus operands
/// (e.g. <c>cli/</c>'s console and the browser <c>stencilApi</c> command surface), but stays a
/// plain value so the parsing is unit-testable without any Telegram types.
/// </remarks>
public sealed record BotCommand(string Verb, string ArgumentText, IReadOnlyList<string> Args);

/// <summary>
/// Pure parser turning a Telegram message body (e.g. <c>"/connect@MyBot http://h tok"</c>)
/// into a <see cref="BotCommand"/>. No Telegram dependencies — deliberately unit-testable.
/// </summary>
public static class CommandParser
{
    /// <summary>
    /// Parse <paramref name="text"/>: strip the leading <c>/name</c> (dropping a trailing
    /// <c>@botname</c>), lowercase the verb, and keep the remaining trimmed argument text plus
    /// its whitespace tokens. A blank or non-slash input yields an empty verb.
    /// </summary>
    public static BotCommand Parse(string? text)
    {
        string trimmed = (text ?? "").Trim();
        if (trimmed.Length == 0 || trimmed[0] != '/')
        {
            return new BotCommand("", "", []);
        }
        int split = IndexOfWhitespace(trimmed);
        string head = split < 0 ? trimmed : trimmed[..split];
        string rest = split < 0 ? "" : trimmed[(split + 1)..].Trim();
        string verb = head[1..];
        int at = verb.IndexOf('@');
        if (at >= 0)
        {
            verb = verb[..at];
        }
        verb = verb.ToLowerInvariant();
        // Normalise the /p shortcut here, once: the router special-cases the prompt verb too
        // (replied-photo adoption, caption edits), so every consumer matches "prompt" alone.
        if (verb == "p")
        {
            verb = "prompt";
        }
        string[] args = rest.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        return new BotCommand(verb, rest, args);
    }

    /// <summary>
    /// Build the synthetic <c>/prompt</c> command the non-slash entry points dispatch (a
    /// chat-mode plain message, an ask-card answer): the same trimmed argument text and
    /// whitespace-token args that <see cref="Parse"/> would produce for "/prompt &lt;text&gt;",
    /// so every synthesis site shares one canonical shape.
    /// </summary>
    public static BotCommand Prompt(string text)
    {
        string spec = (text ?? "").Trim();
        return new BotCommand("prompt", spec, spec.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
    }

    private static int IndexOfWhitespace(string value)
    {
        for (int i = 0; i < value.Length; i++)
        {
            if (char.IsWhiteSpace(value[i]))
            {
                return i;
            }
        }
        return -1;
    }
}
