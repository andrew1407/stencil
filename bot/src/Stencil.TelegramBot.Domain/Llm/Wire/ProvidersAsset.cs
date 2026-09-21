using System.Text.Json;

namespace Stencil.TelegramBot.Domain.Llm.Wire;

// Parsed once from the embedded browser/js/config/llm/providers.json — never a second copy.
public static class ProvidersAsset
{
    private const string _resourceName = "Stencil.TelegramBot.Domain.Assets.providers.json";

    private static readonly Lazy<(string Ollama, string OpenAiCompat, int ChatSeconds)> _parsed = new(load);

    public static string OllamaBaseUrl => _parsed.Value.Ollama;

    public static string OpenAiCompatBaseUrl => _parsed.Value.OpenAiCompat;

    // timeouts.chatSeconds — the cross-surface chat deadline.
    public static int ChatTimeoutSeconds => _parsed.Value.ChatSeconds;

    private static (string, string, int) load()
    {
        using Stream stream = typeof(ProvidersAsset).Assembly.GetManifestResourceStream(_resourceName)
            ?? throw new InvalidOperationException($"embedded resource {_resourceName} is missing");
        using JsonDocument doc = JsonDocument.Parse(stream);
        JsonElement providers = doc.RootElement.GetProperty("providers");
        return (
            providers.GetProperty("ollama").GetProperty("defaultBaseUrl").GetString()!,
            providers.GetProperty("openai-compat").GetProperty("defaultBaseUrl").GetString()!,
            doc.RootElement.GetProperty("timeouts").GetProperty("chatSeconds").GetInt32());
    }
}
