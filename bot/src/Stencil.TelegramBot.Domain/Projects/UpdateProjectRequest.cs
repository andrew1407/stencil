using System.Text.Json;

namespace Stencil.TelegramBot.Domain.Projects;

// protocol UpdateProjectRequest. Version guards the LWW update — a stale one is rejected with
// HTTP 409 / conflict. Every field below is nil-means-unchanged.
public sealed record UpdateProjectRequest
{
    public string? Name { get; init; }
    public string? Color { get; init; }

    // "" clears it.
    public string? Description { get; init; }

    // Only a blank project has one; setting it on an image project has no server-side effect.
    public string? BlankColor { get; init; }

    // Epoch ms; 0 clears the expiry (keep forever).
    public long? ExpiresAt { get; init; }

    public JsonElement? Layout { get; init; }
    public long Version { get; init; }
}
