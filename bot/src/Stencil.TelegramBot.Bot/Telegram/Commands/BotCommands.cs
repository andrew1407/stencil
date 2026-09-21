using System.Text.Json;

namespace Stencil.TelegramBot.Bot.Telegram.Commands;

/// <summary>A null <c>Menu</c> keeps the command off the Telegram "/" menu.</summary>
public sealed record CommandDescriptor(string Verb, string? Menu, IReadOnlyList<string> Aliases);

// The one source for the dispatch aliases, the Telegram "/" menu, /help and the README's command
// tables.
public static class BotCommands
{
    private const string _resourceName = "Stencil.TelegramBot.Bot.Assets.botCommands.json";

    private static readonly Lazy<Loaded> _asset = new(load);

    private sealed record Loaded(
        IReadOnlyList<CommandDescriptor> Commands,
        IReadOnlyDictionary<string, string> Canonical,
        string Help);

    public static IReadOnlyList<CommandDescriptor> All => _asset.Value.Commands;

    public static string HelpText => _asset.Value.Help;

    // Empty when nothing owns the verb — which the dispatch switch answers with /help.
    public static string Canonical(string verb) =>
        _asset.Value.Canonical.TryGetValue(verb, out string? canonical) ? canonical : "";

    private static Loaded load()
    {
        using Stream stream = typeof(BotCommands).Assembly.GetManifestResourceStream(_resourceName)
            ?? throw new InvalidOperationException($"embedded resource {_resourceName} is missing");
        using JsonDocument doc = JsonDocument.Parse(stream);
        List<CommandDescriptor> commands = [];
        Dictionary<string, string> canonical = new(StringComparer.Ordinal);
        foreach (JsonElement entry in doc.RootElement.GetProperty("commands").EnumerateArray())
        {
            string verb = entry.GetProperty("verb").GetString()!;
            string? menu = entry.TryGetProperty("menu", out JsonElement m) ? m.GetString() : null;
            List<string> aliases = [];
            if (entry.TryGetProperty("aliases", out JsonElement list))
            {
                aliases.AddRange(list.EnumerateArray().Select(a => a.GetString()!));
            }
            commands.Add(new CommandDescriptor(verb, menu, aliases));
            canonical[verb] = verb;
            foreach (string alias in aliases)
            {
                canonical[alias] = verb;
            }
        }
        string help = string.Join('\n', doc.RootElement.GetProperty("help")
            .EnumerateArray().Select(l => l.GetString()!));
        return new Loaded(commands, canonical, help);
    }
}
