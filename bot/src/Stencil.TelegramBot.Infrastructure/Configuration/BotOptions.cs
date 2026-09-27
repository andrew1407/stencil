using Stencil.TelegramBot.Domain.Configuration;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Llm.Wire;

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

    // The janitor sweeps every half TTL, never more often than this.
    public TimeSpan WorkspaceSweepFloor { get; init; } = TimeSpan.FromMinutes(_defaultWorkspaceSweepFloorMinutes);

    // Enough that a few minutes-long turns can't starve everyone else; each user holds at most one.
    public int UpdateWorkers { get; init; } = _defaultUpdateWorkers;

    // Updates queued or running across every user; a full pump holds the poller back.
    public int UpdateQueueCapacity { get; init; } = _defaultUpdateQueueCapacity;

    // Updates one user may have waiting behind their own; the rest are dropped, not queued.
    public int MaxPendingPerUser { get; init; } = _defaultMaxPendingPerUser;

    public TimeSpan ShutdownDrainTimeout { get; init; } = TimeSpan.FromSeconds(_defaultDrainTimeoutSeconds);

    // One DNS lookup of a user-supplied link or server host.
    public TimeSpan ResolveTimeout { get; init; } = TimeSpan.FromSeconds(_defaultResolveTimeoutSeconds);

    // The "still working" spinner's beat; Telegram's own chat action fades after ~5 s.
    public TimeSpan ProgressTick { get; init; } = TimeSpan.FromSeconds(_defaultProgressTickSeconds);

    // How often a /sync'ed chat asks its server for a newer project version.
    public TimeSpan SyncPollInterval { get; init; } = TimeSpan.FromSeconds(_defaultSyncPollSeconds);

    // The quiet window that closes an album; Telegram lands its members within about a second.
    public TimeSpan AlbumSettle { get; init; } = TimeSpan.FromMilliseconds(_defaultAlbumSettleMs);

    // Whether /connect may reach loopback and private-LAN servers; link-local never is.
    public bool AllowPrivateServers { get; init; } = true;

    // One server or LLM reply; above the server's 32 MiB body cap once base64 inflates it.
    public long MaxServerResponseBytes { get; init; } = DEFAULT_MAX_SERVER_RESPONSE_BYTES;

    // The ?limit= on each GET /projects page, 1..MAX_PROJECT_LIST_LIMIT; a listing walks every page.
    public int ProjectListLimit { get; init; } = DEFAULT_PROJECT_LIST_LIMIT;

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

    // The server's validate.MaxListLimit.
    public const int MAX_PROJECT_LIST_LIMIT = 500;
    public const int DEFAULT_PROJECT_LIST_LIMIT = 200;
    public const long DEFAULT_MAX_SERVER_RESPONSE_BYTES = (long)_defaultMaxServerResponseMb * 1024 * 1024;

    private const int _defaultHttpTimeoutSeconds = 30;
    private const int _defaultMaxDownloadMb = 50;
    private const int _defaultMaxDocumentMb = 4;
    private const int _defaultWorkspaceTtlMinutes = 60;
    private const int _defaultWorkspaceSweepFloorMinutes = 5;
    private const int _defaultCliTimeoutSeconds = 120;
    private const int _defaultUpdateWorkers = 32;
    private const int _defaultUpdateQueueCapacity = 256;
    private const int _defaultMaxPendingPerUser = 64;
    private const int _defaultDrainTimeoutSeconds = 10;
    private const int _defaultResolveTimeoutSeconds = IBotPolicy.DEFAULT_RESOLVE_TIMEOUT_SECONDS;
    private const int _defaultProgressTickSeconds = 3;
    private const int _defaultSyncPollSeconds = 6;
    private const int _defaultAlbumSettleMs = 1000;
    private const int _defaultMaxServerResponseMb = 64;

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
        WorkspaceSweepFloor = TimeSpan.FromMinutes(EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_WORKSPACE_SWEEP_FLOOR_MINUTES"), _defaultWorkspaceSweepFloorMinutes)),
        CliTimeout = TimeSpan.FromSeconds(EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_CLI_TIMEOUT_SECONDS"), _defaultCliTimeoutSeconds)),
        UpdateWorkers = EnvRead.PositiveInt(EnvRead.Var("STENCIL_BOT_UPDATE_WORKERS"), _defaultUpdateWorkers),
        UpdateQueueCapacity = EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_UPDATE_QUEUE"), _defaultUpdateQueueCapacity),
        MaxPendingPerUser = EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_MAX_PENDING_PER_USER"), _defaultMaxPendingPerUser),
        ShutdownDrainTimeout = TimeSpan.FromSeconds(EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_DRAIN_TIMEOUT_SECONDS"), _defaultDrainTimeoutSeconds)),
        ResolveTimeout = TimeSpan.FromSeconds(EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_RESOLVE_TIMEOUT_SECONDS"), _defaultResolveTimeoutSeconds)),
        ProgressTick = TimeSpan.FromSeconds(EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_PROGRESS_TICK_SECONDS"), _defaultProgressTickSeconds)),
        SyncPollInterval = TimeSpan.FromSeconds(EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_SYNC_POLL_SECONDS"), _defaultSyncPollSeconds)),
        AlbumSettle = TimeSpan.FromMilliseconds(EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_ALBUM_SETTLE_MS"), _defaultAlbumSettleMs)),
        AllowPrivateServers = EnvRead.Flag(EnvRead.Var("STENCIL_BOT_ALLOW_PRIVATE_SERVERS"), fallback: true),
        MaxServerResponseBytes = (long)EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_MAX_SERVER_RESPONSE_MB"), _defaultMaxServerResponseMb) * 1024 * 1024,
        ProjectListLimit = Math.Min(MAX_PROJECT_LIST_LIMIT, EnvRead.PositiveInt(
            EnvRead.Var("STENCIL_BOT_PROJECT_LIST_LIMIT"), DEFAULT_PROJECT_LIST_LIMIT)),
        Llm = LlmProfileOptions.FromEnvironment(),
        LlmProfiles = LlmProfileOptions.ProfilesFromEnvironment(),
        AllowedUsers = EnvRead.UserIds(Environment.GetEnvironmentVariable("STENCIL_BOT_ALLOWED_USERS")),
    };
}
