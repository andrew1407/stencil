namespace Stencil.TelegramBot.Bot.Telegram;

// A plain value so the parsing is unit-testable without Telegram types.
public sealed record BotCommand(string Verb, string ArgumentText, IReadOnlyList<string> Args);

public static class CommandParser
{
    // A trailing @botname is dropped; a blank or non-slash input yields an empty verb.
    public static BotCommand Parse(string? text)
    {
        string trimmed = (text ?? "").Trim();
        if (trimmed.Length == 0 || trimmed[0] != '/')
        {
            return new BotCommand("", "", []);
        }
        int split = indexOfWhitespace(trimmed);
        string head = split < 0 ? trimmed : trimmed[..split];
        string rest = split < 0 ? "" : trimmed[(split + 1)..].Trim();
        string verb = head[1..];
        int at = verb.IndexOf('@');
        if (at >= 0)
        {
            verb = verb[..at];
        }
        verb = verb.ToLowerInvariant();
        // /p is normalised once, here, so every consumer matches "prompt" alone.
        if (verb == "p")
        {
            verb = "prompt";
        }
        string[] args = rest.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        return new BotCommand(verb, rest, args);
    }

    // The synthetic /prompt the non-slash entry points dispatch, in the shape Parse would produce.
    public static BotCommand Prompt(string text)
    {
        string spec = (text ?? "").Trim();
        return new BotCommand("prompt", spec, spec.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
    }

    private static int indexOfWhitespace(string value)
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
