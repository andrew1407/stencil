using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Domain.Configuration;

// The operator policy the presentation layer reads. Infrastructure's BotOptions (which also
// knows how to read the environment) implements it, so a handler names this Domain contract
// instead of that concrete record.
public interface IBotPolicy
{
    bool TlsInsecure { get; }

    // For /link hand-offs.
    string BrowserAppUrl { get; }

    // A file downloaded from Telegram (image/video).
    long MaxDownloadBytes { get; }

    // The far tighter cap for a NON-image upload (a layout or project document).
    long MaxDocumentBytes { get; }

    // How long an unreferenced scratch file may sit before the janitor sweeps it.
    TimeSpan WorkspaceTtl { get; }

    // Empty = the bot is OFF for everyone.
    IReadOnlySet<long> AllowedUsers { get; }

    // In the operator's configured order.
    IReadOnlyList<LlmProfile> LlmProfiles { get; }

    bool AllowedFor(long userId);

    // Case-insensitive; null when the profile is not configured.
    LlmProfile? FindProfile(string? name);
}
