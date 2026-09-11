using System.Text.Json;

namespace Stencil.TelegramBot.Domain.Llm;

// Parsed once from browser/js/config/llm/providers.json, embedded into this assembly at build
// time — the canonical constants, never a second copy.
public static class ProvidersAsset
{
    private const string ResourceName = "Stencil.TelegramBot.Domain.Assets.providers.json";

    private static readonly Lazy<(string Ollama, string OpenAiCompat, int ChatSeconds)> Parsed = new(Load);

    public static string OllamaBaseUrl => Parsed.Value.Ollama;

    public static string OpenAiCompatBaseUrl => Parsed.Value.OpenAiCompat;

    // timeouts.chatSeconds — the cross-surface chat deadline.
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
