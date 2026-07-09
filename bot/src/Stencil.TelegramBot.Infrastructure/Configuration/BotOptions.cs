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
    /// Maximum number of stencil CLI processes allowed to run at once, process-wide
    /// (<c>STENCIL_BOT_MAX_CONCURRENT_CLI</c>). Each edit/probe is a separate OS process, so this
    /// caps process/CPU pressure when many users edit at the same time. Defaults to the CPU count;
    /// values below 1 are clamped up to 1.
    /// </summary>
    public int MaxConcurrentCli { get; init; } = DefaultMaxConcurrentCli;

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

    /// <summary>The default CLI concurrency cap: one process per logical CPU (at least one).</summary>
    private static readonly int DefaultMaxConcurrentCli = Math.Max(1, Environment.ProcessorCount);

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
        int maxConcurrentCli = ParsePositiveInt(
            Environment.GetEnvironmentVariable("STENCIL_BOT_MAX_CONCURRENT_CLI"),
            DefaultMaxConcurrentCli);
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
            MaxConcurrentCli = maxConcurrentCli,
            ServerHttpTimeout = TimeSpan.FromSeconds(httpTimeoutSeconds),
            MaxDownloadBytes = (long)maxDownloadMb * 1024 * 1024,
            WorkspaceTtl = TimeSpan.FromMinutes(workspaceTtlMinutes),
            CliTimeout = TimeSpan.FromSeconds(cliTimeoutSeconds),
        };
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
