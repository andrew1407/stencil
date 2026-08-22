using System.Text.Json;

namespace Stencil.TelegramBot.Domain.Llm;

/// <summary>
/// The canonical provider constants — default base URLs and the shared chat timeout —
/// parsed once from <c>browser/js/config/llm/providers.json</c>, embedded into this
/// assembly at build time (the same pattern as <c>systemPrompt.json</c>).
/// </summary>
public static class ProvidersAsset
{
    private const string ResourceName = "Stencil.TelegramBot.Domain.Assets.providers.json";

    private static readonly Lazy<(string Ollama, string OpenAiCompat, int ChatSeconds)> Parsed = new(Load);

    /// <summary><c>providers.ollama.defaultBaseUrl</c>.</summary>
    public static string OllamaBaseUrl => Parsed.Value.Ollama;

    /// <summary><c>providers.openai-compat.defaultBaseUrl</c>.</summary>
    public static string OpenAiCompatBaseUrl => Parsed.Value.OpenAiCompat;

    /// <summary><c>timeouts.chatSeconds</c> — the cross-surface chat deadline.</summary>
    public static int ChatTimeoutSeconds => Parsed.Value.ChatSeconds;

    private static (string, string, int) Load()
    {
        using Stream stream = typeof(ProvidersAsset).Assembly.GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException($"embedded resource {ResourceName} is missing");
        using JsonDocument doc = JsonDocument.Parse(stream);
        JsonElement providers = doc.RootElement.GetProperty("providers");
        return (
            providers.GetProperty("ollama").GetProperty("defaultBaseUrl").GetString()!,
            providers.GetProperty("openai-compat").GetProperty("defaultBaseUrl").GetString()!,
            doc.RootElement.GetProperty("timeouts").GetProperty("chatSeconds").GetInt32());
    }
}
