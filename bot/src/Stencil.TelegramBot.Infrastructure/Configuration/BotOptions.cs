using Stencil.TelegramBot.Domain.Configuration;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Infrastructure.Configuration;

// Process-wide configuration read from the environment, like the other adapters (mcp's
// STENCIL_CLI override, pystencil's server/TLS knobs). Plain data: injected as a singleton.
// FromEnvironment at the bottom is the property -> env key map.
public sealed record BotOptions : IBotPolicy
{
    public string BotToken { get; init; } = "";

    // Null auto-discovers.
    public string? CliPath { get; init; }

    // Null/blank = in-memory sessions.
    public string? RedisUrl { get; init; }

    public string DataDir { get; init; } = "";

    public bool TlsInsecure { get; init; }

    // What /link builds hand-off links from, through launch.html. A bot link is tapped on
    // someone ELSE's machine, so an operator normally wants a public address here.
    public string BrowserAppUrl { get; init; } = DefaultBrowserAppUrl;

    // Each edit/probe is its own OS process. Values below 1 clamp up to 1.
    public int MaxConcurrentCli { get; init; } = DefaultMaxConcurrentCli;

    // Each call can run for minutes holding image payloads, so a full gate answers "busy"
    // rather than queueing. 0 = unlimited.
    public int MaxConcurrentLlm { get; init; } = DefaultMaxConcurrentLlm;

    // Bounds how long a hung server blocks a handler.
    public TimeSpan ServerHttpTimeout { get; init; } = TimeSpan.FromSeconds(DefaultHttpTimeoutSeconds);

    // Bounds memory/disk against a file downloaded from Telegram.
    public long MaxDownloadBytes { get; init; } = (long)DefaultMaxDownloadMb * 1024 * 1024;

    // A NON-image upload (a layout .json / .stencil project) is parsed WHOLE, hence the far
    // tighter cap — or MaxDownloadBytes when that is smaller.
    public long MaxDocumentBytes => Math.Min(MaxDownloadBytes, (long)DefaultMaxDocumentMb * 1024 * 1024);

    // Without it a slow host could pin a scarce MaxConcurrentCli slot forever.
    public TimeSpan CliTimeout { get; init; } = TimeSpan.FromSeconds(DefaultCliTimeoutSeconds);

    // How long an ORPHANED scratch file may sit; the active image/video are never swept.
    public TimeSpan WorkspaceTtl { get; init; } = TimeSpan.FromMinutes(DefaultWorkspaceTtlMinutes);

    // §5, read from the same STENCIL_LLM_* keys as pystencil; defaults to a local ollama.
    public LlmOptions Llm { get; init; } = new();

    // In the operator's configured order. Empty = the picker is off and every turn uses Llm.
    public IReadOnlyList<LlmProfile> LlmProfiles { get; init; } = [];

    // Falls back to Llm when there is no pick, or the operator has since removed it.
    public LlmOptions LlmFor(string? profileName) =>
        profileName is null ? Llm : FindProfile(profileName)?.Options ?? Llm;

    public LlmProfile? FindProfile(string? name) =>
        name is null ? null : LlmProfiles.FirstOrDefault(p => string.Equals(p.Name, name, StringComparison.OrdinalIgnoreCase));

    // A comma/space-separated list. Empty = OFF for everyone: every command spends the
    // operator's resources, so the bot fails closed and a stranger gets only /start and /help.
    public IReadOnlySet<long> AllowedUsers { get; init; } = new HashSet<long>();

    public bool AllowedFor(long userId) => AllowedUsers.Contains(userId);

    // One CLI process per logical CPU, at least one.
    private static readonly int DefaultMaxConcurrentCli = Math.Max(1, Environment.ProcessorCount);

    // The server's LLM_MAX_IN_FLIGHT default.
    private const int DefaultMaxConcurrentLlm = 8;

    // The served dev app, the base every surface defaults to.
    public const string DefaultBrowserAppUrl = "http://localhost:8080";

    private const int DefaultHttpTimeoutSeconds = 30;
    private const int DefaultMaxDownloadMb = 50;
    private const int DefaultMaxDocumentMb = 4;
    private const int DefaultWorkspaceTtlMinutes = 60;
    private const int DefaultCliTimeoutSeconds = 120;

    public static BotOptions FromEnvironment() => new()
    {
        BotToken = Environment.GetEnvironmentVariable("TELEGRAM_BOT_TOKEN") ?? "",
        CliPath = EnvRead.Var("STENCIL_CLI"),
        RedisUrl = EnvRead.Var("REDIS_URL"),
        DataDir = EnvRead.Var("STENCIL_BOT_DATA_DIR") ?? Path.Combine(Path.GetTempPath(), "stencil-bot"),
        TlsInsecure = EnvRead.Truthy(Environment.GetEnvironmentVariable("STENCIL_TLS_INSECURE")),
        BrowserAppUrl = EnvRead.Var("STENCIL_BOT_BROWSER_URL") ?? DefaultBrowserAppUrl,
        MaxConcurrentCli = EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_MAX_CONCURRENT_CLI"), DefaultMaxConcurrentCli),
        MaxConcurrentLlm = EnvRead.NonNegativeInt(
            EnvRead.Var("STENCIL_BOT_MAX_CONCURRENT_LLM"), DefaultMaxConcurrentLlm),
        ServerHttpTimeout = TimeSpan.FromSeconds(EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_HTTP_TIMEOUT_SECONDS"), DefaultHttpTimeoutSeconds)),
        MaxDownloadBytes = (long)EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_MAX_DOWNLOAD_MB"), DefaultMaxDownloadMb) * 1024 * 1024,
        WorkspaceTtl = TimeSpan.FromMinutes(EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_WORKSPACE_TTL_MINUTES"), DefaultWorkspaceTtlMinutes)),
        CliTimeout = TimeSpan.FromSeconds(EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_CLI_TIMEOUT_SECONDS"), DefaultCliTimeoutSeconds)),
        Llm = LlmProfileOptions.FromEnvironment(),
        LlmProfiles = LlmProfileOptions.ProfilesFromEnvironment(),
        AllowedUsers = EnvRead.UserIds(Environment.GetEnvironmentVariable("STENCIL_BOT_ALLOWED_USERS")),
    };
}
