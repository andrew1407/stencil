using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Bot.Telegram;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Drift tests for the bot's two copy assets. <c>botCommands.json</c> is the single source for
/// four lists that used to be hand-synced — the dispatch table, the Telegram "/" menu,
/// <c>/help</c> and README.md's command tables — so these assert the code and the doc still
/// agree with it; <c>botStrings.json</c> holds the reply/button copy the goldens pin.
/// The README block is generated-but-committed: <c>BOT_UPDATE_PROSE=1 dotnet test</c> rewrites it.
/// </summary>
public sealed class BotAssetTests
{
    private const string _readmeOpen = "<!-- generated from src/Stencil.TelegramBot.Bot/Assets/botCommands.json";
    private const string _readmeClose = "<!-- /generated -->";

    private static JsonElement readAsset(string name) =>
        JsonDocument.Parse(File.ReadAllText(SharedFixtures.PathOf(
            "bot", "src", "Stencil.TelegramBot.Bot", "Assets", name))).RootElement;

    private static bool Updating => Environment.GetEnvironmentVariable("BOT_UPDATE_PROSE") == "1";

    /// <summary>The dispatch table answers every asset command, and nothing the asset omits.</summary>
    [Fact]
    public void TheDispatchTableCoversExactlyTheAssetCommands()
    {
        string[] asset = BotCommands.All.Select(c => c.Verb).Order(StringComparer.Ordinal).ToArray();
        string[] handled = CommandHandlers.HandledVerbs.Order(StringComparer.Ordinal).ToArray();
        Assert.Equal(asset, handled);
    }

    /// <summary>Every alias resolves to its verb; an unowned word resolves to nothing.</summary>
    [Fact]
    public void EveryAliasResolvesToItsCanonicalVerb()
    {
        foreach (CommandDescriptor c in BotCommands.All)
        {
            Assert.Equal(c.Verb, BotCommands.Canonical(c.Verb));
            foreach (string alias in c.Aliases)
            {
                Assert.Equal(c.Verb, BotCommands.Canonical(alias));
            }
        }
        Assert.Equal("", BotCommands.Canonical("nosuchcommand"));
    }

    /// <summary>
    /// The "/" menu is exactly the asset's <c>menu</c> commands, in asset order, and each name is
    /// legal for Telegram (lowercase, ≤32 chars, letters/digits/underscore only).
    /// </summary>
    [Fact]
    public void TheSlashMenuMatchesTheAssetAndTelegramsRules()
    {
        string[] wanted = BotCommands.All.Where(c => c.Menu is not null).Select(c => c.Verb).ToArray();
        Assert.Equal(wanted, BotCommandList.All().Select(c => c.Command).ToArray());
        foreach (var entry in BotCommandList.All())
        {
            Assert.InRange(entry.Command.Length, 1, 32);
            Assert.All(entry.Command, ch => Assert.True(char.IsAsciiLetterLower(ch) || char.IsAsciiDigit(ch) || ch == '_',
                $"/{entry.Command} is not a legal Telegram command name"));
            Assert.False(string.IsNullOrWhiteSpace(entry.Description));
        }
    }

    /// <summary>Every button carries a label and a callback token inside Telegram's 64-byte cap.</summary>
    [Fact]
    public void EveryButtonFitsTelegramsCallbackLimit()
    {
        foreach (JsonProperty button in readAsset("botStrings.json").GetProperty("buttons").EnumerateObject())
        {
            string label = button.Value[0].GetString()!;
            string token = button.Value[1].GetString()!;
            Assert.False(string.IsNullOrWhiteSpace(label), $"button {button.Name} has no label");
            Assert.InRange(Encoding.UTF8.GetByteCount(token), 1, 64);
            Assert.Equal(BotStrings.Button(button.Name).Text, label);
            Assert.Equal(BotStrings.Button(button.Name).CallbackData, token);
        }
    }

    /// <summary>
    /// A command is either documented by a README row or listed as deliberately undocumented —
    /// so a new one forces the choice instead of quietly missing from the docs.
    /// </summary>
    [Fact]
    public void EveryCommandIsDocumentedOrListedAsUndocumented()
    {
        JsonElement readme = readAsset("botCommands.json").GetProperty("readme");
        HashSet<string> documented = readme.GetProperty("tables").EnumerateArray()
            .SelectMany(t => t.GetProperty("rows").EnumerateArray())
            .SelectMany(r => r.GetProperty("verbs").EnumerateArray())
            .Select(v => v.GetString()!)
            .ToHashSet(StringComparer.Ordinal);
        HashSet<string> undocumented = readme.GetProperty("undocumented").EnumerateArray()
            .Select(v => v.GetString()!)
            .ToHashSet(StringComparer.Ordinal);
        HashSet<string> known = BotCommands.All.Select(c => c.Verb)
            .Concat(BotCommands.All.SelectMany(c => c.Aliases))
            .Concat(["p"]) // the /p shortcut CommandParser folds into /prompt
            .ToHashSet(StringComparer.Ordinal);
        Assert.Empty(documented.Except(known).Order(StringComparer.Ordinal));
        Assert.Empty(undocumented.Except(known).Order(StringComparer.Ordinal));
        string[] uncovered = BotCommands.All.Select(c => c.Verb)
            .Where(v => !documented.Contains(v) && !undocumented.Contains(v))
            .Order(StringComparer.Ordinal).ToArray();
        Assert.Empty(uncovered);
    }

    /// <summary>README.md's command tables are generated from the asset and committed.</summary>
    [Fact]
    public void TheReadmeCommandTablesMatchTheAsset()
    {
        StringBuilder sb = new();
        foreach (JsonElement table in readAsset("botCommands.json").GetProperty("readme").GetProperty("tables").EnumerateArray())
        {
            if (sb.Length != 0)
            {
                sb.Append('\n');
            }
            sb.Append(table.GetProperty("heading").GetString()).Append("\n\n| Command | Effect |\n|---|---|\n");
            foreach (JsonElement row in table.GetProperty("rows").EnumerateArray())
            {
                sb.Append("| ").Append(row.GetProperty("command").GetString())
                  .Append(" | ").Append(row.GetProperty("effect").GetString()).Append(" |\n");
            }
        }
        string wanted = sb.ToString();
        string path = SharedFixtures.PathOf("bot", "README.md");
        string readme = File.ReadAllText(path);
        int open = readme.IndexOf(_readmeOpen, StringComparison.Ordinal);
        Assert.True(open >= 0, "bot/README.md lost its generated-block marker");
        int body = readme.IndexOf('\n', open) + 1;
        int close = readme.IndexOf(_readmeClose, body, StringComparison.Ordinal);
        Assert.True(close > body, "bot/README.md lost its <!-- /generated --> marker");
        if (readme[body..close] == wanted)
        {
            return;
        }
        Assert.True(Updating,
            "bot/README.md's command tables are stale against botCommands.json "
            + "(BOT_UPDATE_PROSE=1 dotnet test to rewrite)");
        File.WriteAllText(path, readme[..body] + wanted + readme[close..]);
    }
}
