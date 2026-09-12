using System.Text.Json;

namespace Stencil.TelegramBot.Domain.Projects;

// protocol ProjectResponse; Layout stays raw so unknown fields round-trip.
public sealed record ProjectFull
{
    public required ProjectRecord Project { get; init; }
    public JsonElement? Layout { get; init; }
    public string? OriginalContent { get; init; }
}
