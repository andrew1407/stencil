namespace Stencil.TelegramBot.Bot.Telegram;

// Token → slash command, so a tap and the typed command share one handler; unlisted tokens are bare
// verbs.
internal static class CallbackTokens
{
    private static readonly IReadOnlyDictionary<string, (string Verb, string Args)> _table =
        new Dictionary<string, (string, string)>(StringComparer.Ordinal)
        {
            ["rot90"] = ("rotate", "1"),
            ["rotneg90"] = ("rotate", "-1"),
            ["f:bw"] = ("filter", "bw"),
            ["f:sepia"] = ("filter", "sepia"),
            ["f:invert"] = ("filter", "invert"),
            ["f:contour"] = ("filter", "contour"),
            ["f:none"] = ("filter", "none"),
            ["chat:on"] = ("chat", "on"),
            ["chat:off"] = ("chat", "off"),
            ["chat:clear"] = ("chat", "clear"),
            ["chatclear:confirm"] = ("chat", "clear"),
            ["chat:save-on"] = ("chat", "save on"),
            ["chat:save-off"] = ("chat", "save off"),
            // The bare command replies with the picker / confirmation as a fresh message, so the
            // button works from both menus.
            ["exp:menu"] = ("expire", ""),
            ["del:menu"] = ("delete", ""),
            ["exp:1d"] = ("expire", "1 day"),
            ["exp:3d"] = ("expire", "3 days"),
            ["exp:1w"] = ("expire", "1 week"),
            ["exp:2w"] = ("expire", "fortnight"),
            ["exp:1mo"] = ("expire", "1 month"),
            ["exp:3mo"] = ("expire", "3 months"),
            ["exp:custom"] = ("expire", "custom"),
            ["exp:never"] = ("expire", "never"),
            // Removal only runs from the explicit confirm button; menu/cancel are handled by
            // CallbackAction.
            ["del:confirm"] = ("delete", "confirm"),
        };

    private const string _fetchPrefix = "fetch:";

    // A verb the table names must exist in the command asset — a typo fails at startup.
    static CallbackTokens()
    {
        string[] unknown = [.. _table.Values.Select(static v => v.Verb).Append("fetch")
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
        if (data.StartsWith(_fetchPrefix, StringComparison.Ordinal))
        {
            string id = data[_fetchPrefix.Length..];
            return new BotCommand("fetch", id, [id]);
        }
        (string Verb, string Args) mapped = _table.TryGetValue(data, out (string, string) hit) ? hit : (data, "");
        return new BotCommand(
            mapped.Verb, mapped.Args, mapped.Args.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
    }
}
