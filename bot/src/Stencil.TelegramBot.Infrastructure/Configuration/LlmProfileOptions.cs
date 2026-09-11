using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Infrastructure.Configuration;

/// <summary>
/// The <c>STENCIL_LLM_*</c> half of the bot's configuration (<c>llm-contract.md</c> §5): the one
/// configured provider, plus the optional named profiles <c>/chatapi</c> offers.
/// </summary>
internal static class LlmProfileOptions
{
    /// <summary>
    /// Read the <c>STENCIL_LLM_*</c> keys with the contract's defaults: provider
    /// <c>ollama</c>, and a base URL that follows the chosen provider when not overridden
    /// (ollama <c>http://localhost:11434</c>, openai-compat <c>http://localhost:1234/v1</c>).
    /// </summary>
    public static LlmOptions FromEnvironment()
    {
        string provider = EnvRead.Var("STENCIL_LLM_PROVIDER")?.Trim().ToLowerInvariant() ?? LlmOptions.DefaultProvider;
        return new LlmOptions
        {
            Provider = provider,
            BaseUrl = EnvRead.Var("STENCIL_LLM_BASE_URL") ?? LlmOptions.DefaultBaseUrlFor(provider),
            Model = EnvRead.Var("STENCIL_LLM_MODEL") ?? "",
            ApiKey = EnvRead.Var("STENCIL_LLM_API_KEY") ?? "",
            ServerUrl = EnvRead.Var("STENCIL_LLM_SERVER_URL"),
            ServerToken = EnvRead.Var("STENCIL_LLM_SERVER_TOKEN") ?? "",
        };
    }

    /// <summary>
    /// The selectable chat APIs, from <c>STENCIL_LLM_PROFILES</c> (a comma-separated list of
    /// names) plus one <c>STENCIL_LLM_PROFILE_&lt;NAME&gt;_*</c> group each — <c>LABEL</c>,
    /// <c>PROVIDER</c>, <c>BASE_URL</c>, <c>MODEL</c>, <c>API_KEY</c>, <c>SERVER_URL</c>,
    /// <c>SERVER_TOKEN</c>. Anything a group leaves out falls back to the plain
    /// <c>STENCIL_LLM_*</c> value, so a profile that only changes the model is three lines.
    /// Empty (the default) = no picker; the bot has exactly the one configured provider.
    /// </summary>
    public static IReadOnlyList<LlmProfile> ProfilesFromEnvironment()
    {
        string? names = EnvRead.Var("STENCIL_LLM_PROFILES");
        if (names is null)
        {
            return [];
        }
        LlmOptions fallback = FromEnvironment();
        List<LlmProfile> profiles = [];
        foreach (string raw in names.Split([',', ';'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            string name = raw.ToLowerInvariant();
            string key = $"STENCIL_LLM_PROFILE_{name.ToUpperInvariant().Replace('-', '_')}_";
            string? provider = EnvRead.Var(key + "PROVIDER")?.Trim().ToLowerInvariant();
            // A named profile with no group behind it is a typo in the list, not a silent
            // duplicate of the default — skip it rather than offer a button that changes nothing.
            if (provider is null)
            {
                continue;
            }
            profiles.Add(new LlmProfile
            {
                Name = name,
                Label = EnvRead.Var(key + "LABEL") ?? raw,
                Options = new LlmOptions
                {
                    Provider = provider,
                    BaseUrl = EnvRead.Var(key + "BASE_URL") ?? LlmOptions.DefaultBaseUrlFor(provider),
                    Model = EnvRead.Var(key + "MODEL") ?? "",
                    ApiKey = EnvRead.Var(key + "API_KEY") ?? "",
                    ServerUrl = EnvRead.Var(key + "SERVER_URL") ?? fallback.ServerUrl,
                    ServerToken = EnvRead.Var(key + "SERVER_TOKEN") ?? fallback.ServerToken,
                },
            });
        }
        return profiles;
    }
}
