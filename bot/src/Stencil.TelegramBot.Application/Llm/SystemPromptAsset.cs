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

    private static readonly Lazy<(string Head, string Tail)> Parsed = new(Load);

    /// <summary>The prose BEFORE the "Available ops" list, ending with its newline.</summary>
    public static string Head => Parsed.Value.Head;

    /// <summary>The prose AFTER the ops list, starting with its blank line.</summary>
    public static string Tail => Parsed.Value.Tail;

    private static (string, string) Load()
    {
        using Stream stream = typeof(SystemPromptAsset).Assembly.GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException($"embedded resource {ResourceName} is missing");
        using JsonDocument doc = JsonDocument.Parse(stream);
        return (doc.RootElement.GetProperty("head").GetString()!,
                doc.RootElement.GetProperty("tail").GetString()!);
    }
}
