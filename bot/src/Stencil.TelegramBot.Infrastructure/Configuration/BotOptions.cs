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
        return new BotOptions
        {
            BotToken = token,
            CliPath = cliPath,
            RedisUrl = redisUrl,
            DataDir = dataDir,
            TlsInsecure = tlsInsecure,
        };
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
