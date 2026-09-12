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

    // Empty = the bot is OFF for everyone.
    IReadOnlySet<long> AllowedUsers { get; }

    IReadOnlyList<LlmProfile> LlmProfiles { get; }

    bool AllowedFor(long userId);

    LlmProfile? FindProfile(string? name);
}
