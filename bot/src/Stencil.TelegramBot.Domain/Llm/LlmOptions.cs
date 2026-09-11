namespace Stencil.TelegramBot.Domain.Llm;

// The §5 shape (provider / baseUrl / model / apiKey / serverUrl), read from the same
// STENCIL_LLM_* environment keys pystencil uses.
public sealed record LlmOptions
{
    public const string ProviderOllama = "ollama";
    public const string ProviderOpenAiCompat = "openai-compat";
    public const string ProviderStencilServer = "stencil-server";

    // §5 defaults, from the embedded canonical providers.json.
    public const string DefaultProvider = ProviderOllama;
    public static string DefaultOllamaBaseUrl => ProvidersAsset.OllamaBaseUrl;
    public static string DefaultOpenAiCompatBaseUrl => ProvidersAsset.OpenAiCompatBaseUrl;

    // ollama | openai-compat | stencil-server.
    public string Provider { get; init; } = DefaultProvider;

    // ollama / openai-compat only; the latter already ends in /v1.
    public string BaseUrl { get; init; } = DefaultOllamaBaseUrl;

    // Empty = the provider/server default.
    public string Model { get; init; } = "";

    // Sent as Authorization: Bearer on openai-compat only.
    public string ApiKey { get; init; } = "";

    // stencil-server: which server proxies Anthropic. Null = the invoking user's first connected
    // server, resolved per call from their session.
    public string? ServerUrl { get; init; }

    // With an explicit ServerUrl, the operator's bearer for it — used when the invoking user has
    // no /connect of their own. Empty = the assistant needs them to connect first.
    public string ServerToken { get; init; } = "";

    // The §5 defaults table.
    public static string DefaultBaseUrlFor(string provider) =>
        provider == ProviderOpenAiCompat ? DefaultOpenAiCompatBaseUrl : DefaultOllamaBaseUrl;
}
