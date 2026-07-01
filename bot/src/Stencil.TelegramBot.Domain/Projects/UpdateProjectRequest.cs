using System.Text.Json;

namespace Stencil.TelegramBot.Domain.Projects;

/// <summary>
/// Body of <c>PUT /projects/{id}</c> (protocol <c>UpdateProjectRequest</c>).
/// </summary>
/// <remarks>
/// <see cref="Version"/> guards the last-writer-wins update: a stale version is rejected
/// with HTTP 409 / <c>conflict</c>. <see cref="Name"/> and <see cref="Color"/> follow the
/// nil-means-unchanged contract (null leaves the server value as-is; <c>""</c> for
/// <see cref="Color"/> clears the custom accent).
/// </remarks>
public sealed record UpdateProjectRequest
{
    public string? Name { get; init; }
    public string? Color { get; init; }
    public JsonElement? Layout { get; init; }
    public long Version { get; init; }
}
