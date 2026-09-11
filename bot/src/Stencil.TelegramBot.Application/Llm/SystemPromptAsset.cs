using System.Text.Json;

namespace Stencil.TelegramBot.Application.Llm;

/// <summary>
/// The canonical §4 system-prompt prose around the ops list, parsed once from
/// <c>browser/js/config/llm/systemPrompt.json</c>, embedded into this assembly at build
/// time (the same pattern as the bot's <c>constants.json</c> page-format table).
/// </summary>
public static class SystemPromptAsset
{
    private const string ResourceName = "Stencil.TelegramBot.Application.Assets.systemPrompt.json";

    private static readonly Lazy<IReadOnlyDictionary<string, string>> Parsed = new(Load);

    /// <summary>The prose BEFORE the "Available ops" list, ending with its newline.</summary>
    public static string Head => Text("head");

    /// <summary>The prose AFTER the ops list, starting with its blank line.</summary>
    public static string Tail => Text("tail");

    /// <summary>Any other §4 prose string by its asset key.</summary>
    public static string Text(string key) =>
        Parsed.Value.TryGetValue(key, out string? value)
            ? value
            : throw new InvalidOperationException($"systemPrompt.json has no \"{key}\"");

    private static IReadOnlyDictionary<string, string> Load()
    {
        using Stream stream = typeof(SystemPromptAsset).Assembly.GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException($"embedded resource {ResourceName} is missing");
        using JsonDocument doc = JsonDocument.Parse(stream);
        Dictionary<string, string> map = new(StringComparer.Ordinal);
        foreach (JsonProperty entry in doc.RootElement.EnumerateObject())
        {
            if (entry.Value.ValueKind == JsonValueKind.String)
            {
                map[entry.Name] = entry.Value.GetString()!;
            }
        }
        return map;
    }
}
