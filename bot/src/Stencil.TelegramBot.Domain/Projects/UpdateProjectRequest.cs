using System.Text.Json;

namespace Stencil.TelegramBot.Domain.Projects;

// protocol UpdateProjectRequest: Version guards the LWW update (409 on stale); null = unchanged.
public sealed record UpdateProjectRequest
{
    public string? Name { get; init; }
    public string? Color { get; init; }

    // "" clears it.
    public string? Description { get; init; }

    public string? BlankColor { get; init; }

    // Epoch ms; 0 clears the expiry (keep forever).
    public long? ExpiresAt { get; init; }

    public JsonElement? Layout { get; init; }
    public long Version { get; init; }
}
