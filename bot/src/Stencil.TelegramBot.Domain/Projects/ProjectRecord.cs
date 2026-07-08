namespace Stencil.TelegramBot.Domain.Projects;

/// <summary>
/// Canonical project metadata exchanged over the collaboration server's REST API.
/// </summary>
/// <remarks>
/// A faithful mirror of <c>server/internal/protocol/protocol.go</c> <c>ProjectRecord</c>
/// (the single source of truth every front-end re-declares). Timestamps are epoch
/// milliseconds; <see cref="Source"/> is the media URL, <see cref="Resource"/> the origin
/// page, <see cref="Color"/> a custom accent <c>#rrggbb</c> or <c>""</c> (theme default),
/// and <see cref="Version"/> the monotonic last-writer-wins edit counter.
/// </remarks>
public sealed record ProjectRecord
{
    public string Id { get; init; } = "";
    public string Name { get; init; } = "";
    public long CreatedAt { get; init; }
    public long UpdatedAt { get; init; }

    /// <summary>Expiry (epoch ms; 0/absent = keep forever). Server projects have none by default.</summary>
    public long ExpiresAt { get; init; }
    public bool HasImage { get; init; }
    public int ImageW { get; init; }
    public int ImageH { get; init; }
    public string? Source { get; init; }
    public string? Resource { get; init; }
    public string? Color { get; init; }
    /// <summary>Blank-image fill colour <c>#rrggbb</c> (or <c>""</c> = ordinary image project).</summary>
    public string? BlankColor { get; init; }
    public long Version { get; init; }
}
