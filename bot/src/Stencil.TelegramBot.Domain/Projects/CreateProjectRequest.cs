using System.Text.Json;

namespace Stencil.TelegramBot.Domain.Projects;

/// <summary>
/// Body of <c>POST /projects</c> (protocol <c>CreateProjectRequest</c>). Null fields are
/// dropped by the client so the server applies its own defaults.
/// </summary>
public sealed record CreateProjectRequest
{
    public string? Name { get; init; }
    public string? Source { get; init; }
    public string? Resource { get; init; }
    public string? Color { get; init; }
    public string? Description { get; init; }
    public bool HasImage { get; init; }
    public int ImageW { get; init; }
    public int ImageH { get; init; }
    public JsonElement? Layout { get; init; }
}
