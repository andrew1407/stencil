namespace Stencil.TelegramBot.Domain.Projects;

// A mirror of server/internal/protocol ProjectRecord — the source of truth every front-end
// re-declares. Timestamps are epoch ms; Source is the media URL, Resource the origin page,
// Color a custom accent #rrggbb ("" = theme default), Version the monotonic LWW counter.
public sealed record ProjectRecord
{
    public string Id { get; init; } = "";
    public string Name { get; init; } = "";
    public long CreatedAt { get; init; }
    public long UpdatedAt { get; init; }

    // 0 or absent = keep forever, which is the server default.
    public long ExpiresAt { get; init; }
    public bool HasImage { get; init; }
    public int ImageW { get; init; }
    public int ImageH { get; init; }
    public string? Source { get; init; }
    public string? Resource { get; init; }
    public string? Color { get; init; }
    public string? Description { get; init; }
    // #rrggbb; "" means an ordinary image project, not a blank.
    public string? BlankColor { get; init; }
    public long Version { get; init; }
}
