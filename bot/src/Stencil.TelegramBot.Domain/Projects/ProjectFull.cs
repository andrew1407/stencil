using System.Text.Json;

namespace Stencil.TelegramBot.Domain.Projects;

/// <summary>
/// A single project plus its payload, returned by <c>GET /projects/{id}</c>
/// (protocol <c>ProjectResponse</c>). <see cref="Layout"/> is the raw layout JSON (kept
/// verbatim so unknown fields round-trip); <see cref="OriginalContent"/> is the inline
/// original payload the server keeps for re-fetch (may be empty when bytes live in the
/// file store instead).
/// </summary>
public sealed record ProjectFull
{
    public required ProjectRecord Project { get; init; }
    public JsonElement? Layout { get; init; }
    public string? OriginalContent { get; init; }
}
