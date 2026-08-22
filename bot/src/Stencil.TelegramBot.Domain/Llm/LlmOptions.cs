namespace Stencil.TelegramBot.Domain.Llm;

/// <summary>
/// LLM provider configuration — the bot's copy of the shared shape from
/// <c>llm-contract.md</c> §5 (provider / baseUrl / model / apiKey / serverUrl), read from
/// the same <c>STENCIL_LLM_*</c> environment keys <c>pystencil</c> uses.
/// </summary>
/// <remarks>
/// <see cref="BaseUrl"/>, <see cref="Model"/> and <see cref="ApiKey"/> apply to the
/// <c>ollama</c> / <c>openai-compat</c> providers (the key is optional and sent as a bearer on
/// <c>openai-compat</c> only). <see cref="ServerUrl"/> applies to <c>stencil-server</c> only:
/// which collaboration server proxies Anthropic; when null the invoking user's first connected
/// server is used, authenticated with their existing session token.
/// </remarks>
public sealed record LlmOptions
{
    public const string ProviderOllama = "ollama";
    public const string ProviderOpenAiCompat = "openai-compat";
    public const string ProviderStencilServer = "stencil-server";

    /// <summary>Contract defaults (§5), from the embedded canonical <c>providers.json</c>.</summary>
    public const string DefaultProvider = ProviderOllama;
    public static string DefaultOllamaBaseUrl => ProvidersAsset.OllamaBaseUrl;
    public static string DefaultOpenAiCompatBaseUrl => ProvidersAsset.OpenAiCompatBaseUrl;

    /// <summary><c>ollama</c> | <c>openai-compat</c> | <c>stencil-server</c>.</summary>
    public string Provider { get; init; } = DefaultProvider;

    /// <summary>Endpoint origin for ollama / openai-compat (the latter already ends in <c>/v1</c>).</summary>
    public string BaseUrl { get; init; } = DefaultOllamaBaseUrl;

    /// <summary>Model name; empty = the provider/server default.</summary>
    public string Model { get; init; } = "";

    /// <summary>Optional API key, sent as <c>Authorization: Bearer</c> on openai-compat only.</summary>
    public string ApiKey { get; init; } = "";

    /// <summary>
    /// For <c>stencil-server</c>: the collaboration server that proxies Anthropic. Null = use the
    /// invoking user's first connected server (resolved per call from their session).
    /// </summary>
    public string? ServerUrl { get; init; }

    /// <summary>
    /// For <c>stencil-server</c> with an explicit <see cref="ServerUrl"/>: the operator's bearer
    /// token for that server, used when the invoking user has no <c>/connect</c> of their own to
    /// it. Empty = the assistant needs the user to connect first. Same role as the cli console's
    /// and mcp's <c>STENCIL_LLM_SERVER_TOKEN</c>.
    /// </summary>
    public string ServerToken { get; init; } = "";

    /// <summary>The contract's default base URL for a provider (§5 defaults table).</summary>
    public static string DefaultBaseUrlFor(string provider) =>
        provider == ProviderOpenAiCompat ? DefaultOpenAiCompatBaseUrl : DefaultOllamaBaseUrl;
}
