using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Llm.Wire;

namespace Stencil.TelegramBot.Infrastructure.Configuration;

// The STENCIL_LLM_* half of the configuration (llm-contract.md §5).
internal static class LlmProfileOptions
{
    // The contract's defaults: provider ollama, base URL following the chosen provider when not
    // overridden.
    public static LlmOptions FromEnvironment()
    {
        string provider = EnvRead.Var("STENCIL_LLM_PROVIDER")?.Trim().ToLowerInvariant() ?? LlmOptions.DEFAULT_PROVIDER;
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

    // STENCIL_LLM_PROFILES names the profiles; each STENCIL_LLM_PROFILE_<NAME>_* key (LABEL, PROVIDER,
    // BASE_URL, MODEL, API_KEY, SERVER_URL, SERVER_TOKEN) falls back to the plain STENCIL_LLM_* value.
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
            // A named profile with no group behind it is a typo, not a silent duplicate of the
            // default: skip it.
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
