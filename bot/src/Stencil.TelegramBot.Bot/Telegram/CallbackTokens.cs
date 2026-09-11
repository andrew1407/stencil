namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// The callback token → slash-command table, so a tap and the typed command share one handler.
/// It is a table for the same reason <see cref="CommandHandlers.DispatchAsync"/>'s is: the verbs
/// are the vocabulary in <c>botCommands.json</c>, checked here at startup, and a new button is
/// one row. Tokens the table does not name are bare verbs, dispatched as-is.
/// </summary>
internal static class CallbackTokens
{
    /// <summary>Token → (canonical verb, argument text). The args list is the text, split.</summary>
    private static readonly IReadOnlyDictionary<string, (string Verb, string Args)> Table =
        new Dictionary<string, (string, string)>(StringComparer.Ordinal)
        {
            ["rot90"] = ("rotate", "1"),
            ["rotneg90"] = ("rotate", "-1"),
            ["f:bw"] = ("filter", "bw"),
            ["f:sepia"] = ("filter", "sepia"),
            ["f:invert"] = ("filter", "invert"),
            ["f:contour"] = ("filter", "contour"),
            ["f:none"] = ("filter", "none"),
            // The Chat buttons ride the equivalent /chat on|off command, so a tap and the slash
            // command share one handler (and one confirmation).
            ["chat:on"] = ("chat", "on"),
            ["chat:off"] = ("chat", "off"),
            ["chat:clear"] = ("chat", "clear"),
            // The §10 clearChat Yes button — the same /chat clear the 🧹 button dispatches.
            ["chatclear:confirm"] = ("chat", "clear"),
            // The 💾 Save-chats toggle rides the equivalent /chat save on|off command (§12.3).
            ["chat:save-on"] = ("chat", "save on"),
            ["chat:save-off"] = ("chat", "save off"),
            // The Expiration / Remove entry buttons dispatch the bare command, which replies with
            // the duration picker / delete confirmation as a fresh message (so the same button
            // works from both the status menu and the image edit menu).
            ["exp:menu"] = ("expire", ""),
            ["del:menu"] = ("delete", ""),
            // Expiry presets ride the equivalent /expire command; "custom" opens the free-text
            // prompt and "never" clears the expiry — both handled inside the /expire handler.
            ["exp:1d"] = ("expire", "1 day"),
            ["exp:3d"] = ("expire", "3 days"),
            ["exp:1w"] = ("expire", "1 week"),
            ["exp:2w"] = ("expire", "fortnight"),
            ["exp:1mo"] = ("expire", "1 month"),
            ["exp:3mo"] = ("expire", "3 months"),
            ["exp:custom"] = ("expire", "custom"),
            ["exp:never"] = ("expire", "never"),
            // Delete's actual removal only runs from the explicit confirm button; the menu/cancel
            // tokens are handled by CallbackAction (dispatch the confirmation / retire it).
            ["del:confirm"] = ("delete", "confirm"),
        };

    /// <summary>The prefix token that carries a project id as its argument.</summary>
    private const string FetchPrefix = "fetch:";

    /// <summary>A verb the table names must exist in the command asset — a typo fails at startup.</summary>
    static CallbackTokens()
    {
        string[] unknown = [.. Table.Values.Select(static v => v.Verb).Append("fetch")
            .Where(static verb => BotCommands.Canonical(verb).Length == 0)
            .Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal)];
        if (unknown.Length > 0)
        {
            throw new InvalidOperationException(
                $"callback tokens dispatch to verb(s) botCommands.json does not define: {string.Join(", ", unknown)}");
        }
    }

    public static BotCommand Map(string data)
    {
        if (data.StartsWith(FetchPrefix, StringComparison.Ordinal))
        {
            string id = data[FetchPrefix.Length..];
            return new BotCommand("fetch", id, [id]);
        }
        // All other unlisted tokens are bare verbs (help/projects/create/save/status/image/
        // json/reset/undoline/clearlines), dispatched as-is.
        (string Verb, string Args) mapped = Table.TryGetValue(data, out (string, string) hit) ? hit : (data, "");
        return new BotCommand(
            mapped.Verb, mapped.Args, mapped.Args.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
    }
}
