using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Domain.Configuration;

public interface IBotPolicy
{
    bool TlsInsecure { get; }

    string BrowserAppUrl { get; }

    long MaxDownloadBytes { get; }

    // The far tighter cap for a non-image upload (a layout or project document).
    long MaxDocumentBytes { get; }

    TimeSpan WorkspaceTtl { get; }

    // The shortest gap between two workspace sweeps, however short the TTL.
    TimeSpan WorkspaceSweepFloor { get; }

    // One DNS lookup of a user-supplied link or server host.
    TimeSpan ResolveTimeout { get; }

    // ResolveTimeout's default, in seconds, wherever no configured policy is at hand.
    const int DEFAULT_RESOLVE_TIMEOUT_SECONDS = 5;

    TimeSpan ProgressTick { get; }

    TimeSpan SyncPollInterval { get; }

    // The quiet window after an album's last member before the group is handled.
    TimeSpan AlbumSettle { get; }

    // Whether /connect may reach loopback and private-LAN servers.
    bool AllowPrivateServers { get; }

    // Empty = the bot is OFF for everyone.
    IReadOnlySet<long> AllowedUsers { get; }

    IReadOnlyList<LlmProfile> LlmProfiles { get; }

    bool AllowedFor(long userId);

    LlmProfile? FindProfile(string? name);
}
