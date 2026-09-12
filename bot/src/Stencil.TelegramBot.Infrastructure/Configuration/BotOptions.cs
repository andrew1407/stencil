using Stencil.TelegramBot.Domain.Configuration;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Infrastructure.Configuration;

// Read from the environment like the other adapters; FromEnvironment at the bottom is the property
// -> env key map.
public sealed record BotOptions : IBotPolicy
{
    public string BotToken { get; init; } = "";

    // Null auto-discovers.
    public string? CliPath { get; init; }

    // Null/blank = in-memory sessions.
    public string? RedisUrl { get; init; }

    public string DataDir { get; init; } = "";

    public bool TlsInsecure { get; init; }

    // A bot link is tapped on someone ELSE's machine, so an operator normally wants a public
    // address.
    public string BrowserAppUrl { get; init; } = DEFAULT_BROWSER_APP_URL;

    // Each edit/probe is its own OS process. Values below 1 clamp up to 1.
    public int MaxConcurrentCli { get; init; } = _defaultMaxConcurrentCli;

    // A full gate answers "busy" rather than queueing (calls run for minutes holding images). 0 =
    // unlimited.
    public int MaxConcurrentLlm { get; init; } = _defaultMaxConcurrentLlm;

    public TimeSpan ServerHttpTimeout { get; init; } = TimeSpan.FromSeconds(_defaultHttpTimeoutSeconds);

    public long MaxDownloadBytes { get; init; } = (long)_defaultMaxDownloadMb * 1024 * 1024;

    // A NON-image upload is parsed WHOLE, hence the far tighter cap.
    public long MaxDocumentBytes => Math.Min(MaxDownloadBytes, (long)_defaultMaxDocumentMb * 1024 * 1024);

    // Without it a slow host could pin a scarce MaxConcurrentCli slot forever.
    public TimeSpan CliTimeout { get; init; } = TimeSpan.FromSeconds(_defaultCliTimeoutSeconds);

    // How long an ORPHANED scratch file may sit; the active image/video are never swept.
    public TimeSpan WorkspaceTtl { get; init; } = TimeSpan.FromMinutes(_defaultWorkspaceTtlMinutes);

    // §5, read from the same STENCIL_LLM_* keys as pystencil; defaults to a local ollama.
    public LlmOptions Llm { get; init; } = new();

    // In the operator's configured order. Empty = the picker is off and every turn uses Llm.
    public IReadOnlyList<LlmProfile> LlmProfiles { get; init; } = [];

    // Falls back to Llm when there is no pick, or the operator has since removed it.
    public LlmOptions LlmFor(string? profileName) =>
        profileName is null ? Llm : FindProfile(profileName)?.Options ?? Llm;

    public LlmProfile? FindProfile(string? name) =>
        name is null ? null : LlmProfiles.FirstOrDefault(p => string.Equals(p.Name, name, StringComparison.OrdinalIgnoreCase));

    // Empty = OFF for everyone: the bot fails closed and a stranger gets only /start and /help.
    public IReadOnlySet<long> AllowedUsers { get; init; } = new HashSet<long>();

    public bool AllowedFor(long userId) => AllowedUsers.Contains(userId);

    // One CLI process per logical CPU, at least one.
    private static readonly int _defaultMaxConcurrentCli = Math.Max(1, Environment.ProcessorCount);

    // The server's LLM_MAX_IN_FLIGHT default.
    private const int _defaultMaxConcurrentLlm = 8;

    // The served dev app, the base every surface defaults to.
    public const string DEFAULT_BROWSER_APP_URL = "http://localhost:8080";

    private const int _defaultHttpTimeoutSeconds = 30;
    private const int _defaultMaxDownloadMb = 50;
    private const int _defaultMaxDocumentMb = 4;
    private const int _defaultWorkspaceTtlMinutes = 60;
    private const int _defaultCliTimeoutSeconds = 120;

    public static BotOptions FromEnvironment() => new()
    {
        BotToken = Environment.GetEnvironmentVariable("TELEGRAM_BOT_TOKEN") ?? "",
        CliPath = EnvRead.Var("STENCIL_CLI"),
        RedisUrl = EnvRead.Var("REDIS_URL"),
        DataDir = EnvRead.Var("STENCIL_BOT_DATA_DIR") ?? Path.Combine(Path.GetTempPath(), "stencil-bot"),
        TlsInsecure = EnvRead.Truthy(Environment.GetEnvironmentVariable("STENCIL_TLS_INSECURE")),
        BrowserAppUrl = EnvRead.Var("STENCIL_BOT_BROWSER_URL") ?? DEFAULT_BROWSER_APP_URL,
        MaxConcurrentCli = EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_MAX_CONCURRENT_CLI"), _defaultMaxConcurrentCli),
        MaxConcurrentLlm = EnvRead.NonNegativeInt(
            EnvRead.Var("STENCIL_BOT_MAX_CONCURRENT_LLM"), _defaultMaxConcurrentLlm),
        ServerHttpTimeout = TimeSpan.FromSeconds(EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_HTTP_TIMEOUT_SECONDS"), _defaultHttpTimeoutSeconds)),
        MaxDownloadBytes = (long)EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_MAX_DOWNLOAD_MB"), _defaultMaxDownloadMb) * 1024 * 1024,
        WorkspaceTtl = TimeSpan.FromMinutes(EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_WORKSPACE_TTL_MINUTES"), _defaultWorkspaceTtlMinutes)),
        CliTimeout = TimeSpan.FromSeconds(EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_CLI_TIMEOUT_SECONDS"), _defaultCliTimeoutSeconds)),
        Llm = LlmProfileOptions.FromEnvironment(),
        LlmProfiles = LlmProfileOptions.ProfilesFromEnvironment(),
        AllowedUsers = EnvRead.UserIds(Environment.GetEnvironmentVariable("STENCIL_BOT_ALLOWED_USERS")),
    };
}
