using System.Text.Json;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>A null <c>Menu</c> keeps the command off the Telegram "/" menu.</summary>
public sealed record CommandDescriptor(string Verb, string? Menu, IReadOnlyList<string> Aliases);

/// <summary>
/// The bot's command vocabulary, parsed once from the embedded <c>Assets/botCommands.json</c>
/// (same embedding pattern as <see cref="PageFormats"/>'s <c>constants.json</c>). It is the one
/// source for four lists: the dispatch table's aliases
/// (<see cref="Canonical"/>), the Telegram "/" menu (<see cref="BotCommandList"/>),
/// <c>/help</c> (<see cref="HelpText"/>) and the README's command tables.
/// </summary>
public static class BotCommands
{
    private const string ResourceName = "Stencil.TelegramBot.Bot.Assets.botCommands.json";

    private static readonly Lazy<Loaded> Asset = new(Load);

    private sealed record Loaded(
        IReadOnlyList<CommandDescriptor> Commands,
        IReadOnlyDictionary<string, string> Canonical,
        string Help);

    /// <summary>Every command, menu order first, then the ones that stay off the "/" menu.</summary>
    public static IReadOnlyList<CommandDescriptor> All => Asset.Value.Commands;

    public static string HelpText => Asset.Value.Help;

    /// <summary>
    /// The canonical verb a typed verb dispatches to (<c>project-color</c> → <c>projectcolor</c>),
    /// or an empty string when nothing owns it — which the dispatch switch answers with /help.
    /// </summary>
    public static string Canonical(string verb) =>
        Asset.Value.Canonical.TryGetValue(verb, out string? canonical) ? canonical : "";

    private static Loaded Load()
    {
        using Stream stream = typeof(BotCommands).Assembly.GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException($"embedded resource {ResourceName} is missing");
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
