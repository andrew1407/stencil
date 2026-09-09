using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Infrastructure.Configuration;

/// <summary>
/// Process-wide configuration, read from the environment (mirrors the env-driven
/// configuration of the other adapters — <c>mcp/</c>'s <c>STENCIL_CLI</c> override and
/// <c>pystencil</c>'s server/TLS knobs). Plain data so it can be injected as a singleton.
/// </summary>
/// <remarks>
/// <see cref="BotToken"/> is the Telegram bot API token; <see cref="CliPath"/> overrides CLI
/// discovery (<c>STENCIL_CLI</c>); <see cref="RedisUrl"/> selects the Redis-backed session
/// store when present; <see cref="DataDir"/> is the per-user scratch root; and
/// <see cref="TlsInsecure"/> accepts self-signed certs on dev collaboration servers.
/// </remarks>
public sealed record BotOptions
{
    /// <summary>Telegram bot API token (<c>TELEGRAM_BOT_TOKEN</c>).</summary>
    public string BotToken { get; init; } = "";

    /// <summary>Explicit CLI binary path (<c>STENCIL_CLI</c>), or null to auto-discover.</summary>
    public string? CliPath { get; init; }

    /// <summary>Redis connection string (<c>REDIS_URL</c>); null/blank ⇒ in-memory sessions.</summary>
    public string? RedisUrl { get; init; }

    /// <summary>Per-user scratch directory root (<c>STENCIL_BOT_DATA_DIR</c>).</summary>
    public string DataDir { get; init; } = "";

    /// <summary>When true, skip TLS certificate validation for collaboration servers.</summary>
    public bool TlsInsecure { get; init; }

    /// <summary>
    /// Base URL of the served browser app (<c>STENCIL_BOT_BROWSER_URL</c>), e.g.
    /// <c>https://stencil.example/app</c>. It is what <c>/link</c> builds its desktop hand-off
    /// links from — they ride through that app's <c>launch.html</c> bounce page. Null (the
    /// default) ⇒ <c>/link</c> replies that the operator hasn't configured one.
    /// </summary>
    public string? BrowserAppUrl { get; init; }

    /// <summary>
    /// Maximum number of stencil CLI processes allowed to run at once, process-wide
    /// (<c>STENCIL_BOT_MAX_CONCURRENT_CLI</c>). Each edit/probe is a separate OS process, so this
    /// caps process/CPU pressure when many users edit at the same time. Defaults to the CPU count;
    /// values below 1 are clamped up to 1.
    /// </summary>
    public int MaxConcurrentCli { get; init; } = DefaultMaxConcurrentCli;

    /// <summary>
    /// Maximum number of LLM calls in flight at once, process-wide
    /// (<c>STENCIL_BOT_MAX_CONCURRENT_LLM</c>). Each call can run for minutes holding image
    /// payloads, so this bounds memory and upstream spend when many users prompt at once; a
    /// full gate answers "busy" immediately rather than queueing. Default 8 (the server's
    /// <c>LLM_MAX_IN_FLIGHT</c> default); 0 = unlimited.
    /// </summary>
    public int MaxConcurrentLlm { get; init; } = DefaultMaxConcurrentLlm;

    /// <summary>
    /// Timeout for a single collaboration-server REST request (<c>STENCIL_BOT_HTTP_TIMEOUT_SECONDS</c>).
    /// Bounds how long a slow/hung server can block an update handler. Default 30s.
    /// </summary>
    public TimeSpan ServerHttpTimeout { get; init; } = TimeSpan.FromSeconds(DefaultHttpTimeoutSeconds);

    /// <summary>
    /// Maximum size, in bytes, of a file the bot will download from Telegram
    /// (<c>STENCIL_BOT_MAX_DOWNLOAD_MB</c>). Caps memory/disk from an oversized upload. Default 50 MB.
    /// </summary>
    public long MaxDownloadBytes { get; init; } = (long)DefaultMaxDownloadMb * 1024 * 1024;

    /// <summary>
    /// Maximum wall-clock time a single stencil CLI invocation may run
    /// (<c>STENCIL_BOT_CLI_TIMEOUT_SECONDS</c>) before it is killed. A scrape fetches a page plus
    /// N media downloads, so without a bound a slow/hung host could pin a scarce
    /// <see cref="MaxConcurrentCli"/> slot indefinitely and starve the bot. Default 120s.
    /// </summary>
    public TimeSpan CliTimeout { get; init; } = TimeSpan.FromSeconds(DefaultCliTimeoutSeconds);

    /// <summary>
    /// How long an unreferenced per-user scratch file may sit before the janitor sweeps it
    /// (<c>STENCIL_BOT_WORKSPACE_TTL_MINUTES</c>). The active image/video are never swept, only the
    /// orphaned render/layout artifacts. Default 60 min.
    /// </summary>
    public TimeSpan WorkspaceTtl { get; init; } = TimeSpan.FromMinutes(DefaultWorkspaceTtlMinutes);

    /// <summary>
    /// LLM assistant configuration (<c>llm-contract.md</c> §5), read from the same
    /// <c>STENCIL_LLM_PROVIDER</c> / <c>STENCIL_LLM_BASE_URL</c> / <c>STENCIL_LLM_MODEL</c> /
    /// <c>STENCIL_LLM_API_KEY</c> / <c>STENCIL_LLM_SERVER_URL</c> keys as <c>pystencil</c>.
    /// Defaults to a local ollama at its native port.
    /// </summary>
    public LlmOptions Llm { get; init; } = new();

    /// <summary>
    /// The chat APIs <c>/chatapi</c> offers, in the operator's configured order. Empty = the
    /// picker is off and every turn uses <see cref="Llm"/>. See LlmProfilesFromEnvironment.
    /// </summary>
    public IReadOnlyList<LlmProfile> LlmProfiles { get; init; } = [];

    /// <summary>
    /// The configuration a user's picked profile selects, or <see cref="Llm"/> when they have
    /// picked nothing — or picked something the operator has since removed.
    /// </summary>
    public LlmOptions LlmFor(string? profileName) =>
        profileName is null ? Llm : FindProfile(profileName)?.Options ?? Llm;

    /// <summary>The profile by name (case-insensitively), or null when it is not configured.</summary>
    public LlmProfile? FindProfile(string? name) =>
        name is null ? null : LlmProfiles.FirstOrDefault(p => string.Equals(p.Name, name, StringComparison.OrdinalIgnoreCase));

    /// <summary>
    /// Telegram user ids allowed to use the LLM commands (<c>STENCIL_BOT_ALLOWED_USERS</c>, a
    /// comma/space-separated list). Empty = the assistant is OFF for everyone.
    /// </summary>
    /// <remarks>
    /// Every LLM turn spends the operator's single <c>STENCIL_LLM_API_KEY</c> and anyone who finds
    /// the bot can message it, so the assistant is opt-in per user and fails closed. Editing is
    /// unaffected.
    /// </remarks>
    public IReadOnlySet<long> LlmAllowedUsers { get; init; } = new HashSet<long>();

    /// <summary>Whether <paramref name="userId"/> may use <c>/prompt</c> and <c>/chat</c>.</summary>
    public bool LlmAllowedFor(long userId) => LlmAllowedUsers.Contains(userId);

    /// <summary>The default CLI concurrency cap: one process per logical CPU (at least one).</summary>
    private static readonly int DefaultMaxConcurrentCli = Math.Max(1, Environment.ProcessorCount);

    /// <summary>The default LLM in-flight cap: the server's <c>LLM_MAX_IN_FLIGHT</c> default.</summary>
    private const int DefaultMaxConcurrentLlm = 8;

    private const int DefaultHttpTimeoutSeconds = 30;
    private const int DefaultMaxDownloadMb = 50;
    private const int DefaultWorkspaceTtlMinutes = 60;
    private const int DefaultCliTimeoutSeconds = 120;

    /// <summary>
    /// Build options from the current process environment. The data-dir defaults to
    /// <c>&lt;temp&gt;/stencil-bot</c>; <see cref="TlsInsecure"/> is the truthy reading of
    /// <c>STENCIL_TLS_INSECURE</c> (<c>1</c>/<c>true</c>/<c>yes</c>, case-insensitive).
    /// </summary>
    public static BotOptions FromEnvironment()
    {
        string token = Environment.GetEnvironmentVariable("TELEGRAM_BOT_TOKEN") ?? "";
        string? cliPath = NullIfBlank(Environment.GetEnvironmentVariable("STENCIL_CLI"));
        string? redisUrl = NullIfBlank(Environment.GetEnvironmentVariable("REDIS_URL"));
        string dataDir = NullIfBlank(Environment.GetEnvironmentVariable("STENCIL_BOT_DATA_DIR"))
            ?? Path.Combine(Path.GetTempPath(), "stencil-bot");
        bool tlsInsecure = IsTruthy(Environment.GetEnvironmentVariable("STENCIL_TLS_INSECURE"));
        string? browserAppUrl = NullIfBlank(Environment.GetEnvironmentVariable("STENCIL_BOT_BROWSER_URL"));
        int maxConcurrentCli = ParsePositiveInt(
            Environment.GetEnvironmentVariable("STENCIL_BOT_MAX_CONCURRENT_CLI"),
            DefaultMaxConcurrentCli);
        int maxConcurrentLlm = ParseNonNegativeInt(
            Environment.GetEnvironmentVariable("STENCIL_BOT_MAX_CONCURRENT_LLM"),
            DefaultMaxConcurrentLlm);
        int httpTimeoutSeconds = ParsePositiveInt(
            Environment.GetEnvironmentVariable("STENCIL_BOT_HTTP_TIMEOUT_SECONDS"),
            DefaultHttpTimeoutSeconds);
        int maxDownloadMb = ParsePositiveInt(
            Environment.GetEnvironmentVariable("STENCIL_BOT_MAX_DOWNLOAD_MB"),
            DefaultMaxDownloadMb);
        int workspaceTtlMinutes = ParsePositiveInt(
            Environment.GetEnvironmentVariable("STENCIL_BOT_WORKSPACE_TTL_MINUTES"),
            DefaultWorkspaceTtlMinutes);
        int cliTimeoutSeconds = ParsePositiveInt(
            Environment.GetEnvironmentVariable("STENCIL_BOT_CLI_TIMEOUT_SECONDS"),
            DefaultCliTimeoutSeconds);
        return new BotOptions
        {
            BotToken = token,
            CliPath = cliPath,
            RedisUrl = redisUrl,
            DataDir = dataDir,
            TlsInsecure = tlsInsecure,
            BrowserAppUrl = browserAppUrl,
            MaxConcurrentCli = maxConcurrentCli,
            MaxConcurrentLlm = maxConcurrentLlm,
            ServerHttpTimeout = TimeSpan.FromSeconds(httpTimeoutSeconds),
            MaxDownloadBytes = (long)maxDownloadMb * 1024 * 1024,
            WorkspaceTtl = TimeSpan.FromMinutes(workspaceTtlMinutes),
            CliTimeout = TimeSpan.FromSeconds(cliTimeoutSeconds),
            Llm = LlmFromEnvironment(),
            LlmProfiles = LlmProfilesFromEnvironment(),
            LlmAllowedUsers = ParseUserIds(Environment.GetEnvironmentVariable("STENCIL_BOT_ALLOWED_USERS")),
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
    private static IReadOnlyList<LlmProfile> LlmProfilesFromEnvironment()
    {
        string? names = NullIfBlank(Environment.GetEnvironmentVariable("STENCIL_LLM_PROFILES"));
        if (names is null)
        {
            return [];
        }
        LlmOptions fallback = LlmFromEnvironment();
        List<LlmProfile> profiles = [];
        foreach (string raw in names.Split([',', ';'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            string name = raw.ToLowerInvariant();
            string key = $"STENCIL_LLM_PROFILE_{name.ToUpperInvariant().Replace('-', '_')}_";
            string? provider = NullIfBlank(Environment.GetEnvironmentVariable(key + "PROVIDER"))?.Trim().ToLowerInvariant();
            // A named profile with no group behind it is a typo in the list, not a silent
            // duplicate of the default — skip it rather than offer a button that changes nothing.
            if (provider is null)
            {
                continue;
            }
            profiles.Add(new LlmProfile
            {
                Name = name,
                Label = NullIfBlank(Environment.GetEnvironmentVariable(key + "LABEL")) ?? raw,
                Options = new LlmOptions
                {
                    Provider = provider,
                    BaseUrl = NullIfBlank(Environment.GetEnvironmentVariable(key + "BASE_URL"))
                        ?? LlmOptions.DefaultBaseUrlFor(provider),
                    Model = NullIfBlank(Environment.GetEnvironmentVariable(key + "MODEL")) ?? "",
                    ApiKey = NullIfBlank(Environment.GetEnvironmentVariable(key + "API_KEY")) ?? "",
                    ServerUrl = NullIfBlank(Environment.GetEnvironmentVariable(key + "SERVER_URL")) ?? fallback.ServerUrl,
                    ServerToken = NullIfBlank(Environment.GetEnvironmentVariable(key + "SERVER_TOKEN")) ?? fallback.ServerToken,
                },
            });
        }
        return profiles;
    }

    /// <summary>
    /// Read the <c>STENCIL_LLM_*</c> keys with the contract's defaults: provider
    /// <c>ollama</c>, and a base URL that follows the chosen provider when not overridden
    /// (ollama <c>http://localhost:11434</c>, openai-compat <c>http://localhost:1234/v1</c>).
    /// </summary>
    private static LlmOptions LlmFromEnvironment()
    {
        string provider = NullIfBlank(Environment.GetEnvironmentVariable("STENCIL_LLM_PROVIDER"))
            ?.Trim().ToLowerInvariant() ?? LlmOptions.DefaultProvider;
        return new LlmOptions
        {
            Provider = provider,
            BaseUrl = NullIfBlank(Environment.GetEnvironmentVariable("STENCIL_LLM_BASE_URL"))
                ?? LlmOptions.DefaultBaseUrlFor(provider),
            Model = NullIfBlank(Environment.GetEnvironmentVariable("STENCIL_LLM_MODEL")) ?? "",
            ApiKey = NullIfBlank(Environment.GetEnvironmentVariable("STENCIL_LLM_API_KEY")) ?? "",
            ServerUrl = NullIfBlank(Environment.GetEnvironmentVariable("STENCIL_LLM_SERVER_URL")),
            ServerToken = NullIfBlank(Environment.GetEnvironmentVariable("STENCIL_LLM_SERVER_TOKEN")) ?? "",
        };
    }

    /// <summary>
    /// Parse a comma/space/semicolon-separated list of Telegram user ids. An unparseable entry is
    /// dropped: a typo must neither widen the list nor stop the bot from starting.
    /// </summary>
    private static IReadOnlySet<long> ParseUserIds(string? value)
    {
        HashSet<long> ids = [];
        if (string.IsNullOrWhiteSpace(value))
        {
            return ids;
        }
        foreach (string part in value.Split([',', ' ', ';', '\t'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            if (long.TryParse(part, System.Globalization.NumberStyles.AllowLeadingSign,
                    System.Globalization.CultureInfo.InvariantCulture, out long id)
                && id != 0)
            {
                ids.Add(id);
            }
        }
        return ids;
    }

    /// <summary>Parse a positive integer, falling back to <paramref name="fallback"/> when unset or invalid.</summary>
    private static int ParsePositiveInt(string? value, int fallback)
    {
        if (int.TryParse(value, out int parsed) && parsed >= 1)
        {
            return parsed;
        }
        return fallback;
    }

    /// <summary>Parse a non-negative integer (0 allowed), falling back when unset or invalid.</summary>
    private static int ParseNonNegativeInt(string? value, int fallback)
    {
        if (int.TryParse(value, out int parsed) && parsed >= 0)
        {
            return parsed;
        }
        return fallback;
    }

    /// <summary>Treat <c>1</c>/<c>true</c>/<c>yes</c> (any case, trimmed) as true.</summary>
    private static bool IsTruthy(string? value)
    {
        if (value is null)
        {
            return false;
        }
        string trimmed = value.Trim().ToLowerInvariant();
        return trimmed is "1" or "true" or "yes";
    }

    /// <summary>Collapse a missing/whitespace value to null.</summary>
    private static string? NullIfBlank(string? value) =>
        string.IsNullOrWhiteSpace(value) ? null : value;
}
