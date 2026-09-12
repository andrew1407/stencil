using System.Text.Json;

namespace Stencil.TelegramBot.Application.Llm;

// The canonical §4 prose, parsed once from the embedded browser/js/config/llm/systemPrompt.json.
public static class SystemPromptAsset
{
    private const string ResourceName = "Stencil.TelegramBot.Application.Assets.systemPrompt.json";

    private static readonly Lazy<IReadOnlyDictionary<string, string>> Parsed = new(Load);

    /// <summary>The prose BEFORE the "Available ops" list, ending with its newline.</summary>
    public static string Head => Text("head");

    /// <summary>The prose AFTER the ops list, starting with its blank line.</summary>
    public static string Tail => Text("tail");

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
