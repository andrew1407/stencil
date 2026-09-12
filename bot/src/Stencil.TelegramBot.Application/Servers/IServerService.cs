using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Servers;

// A port of pystencil's ConnectionManager + remoteSync (REST only, no live feed). Every Set*/Save
// below is version-guarded and throws on a last-writer-wins conflict.
public interface IServerService
{
    // Deduped by normalised URL.
    Task<ServerConnectionInfo> ConnectAsync(long userId, string url, string? token, bool verifyTls, CancellationToken ct = default);

    // The named connection, or the most recently added when url is omitted.
    Task<bool> DisconnectAsync(long userId, string? url, CancellationToken ct = default);

    Task<IReadOnlyList<ServerConnectionInfo>> ConnectionsAsync(long userId, CancellationToken ct = default);

    // One connection when url is given, else every one; an unreachable server is skipped.
    Task<IReadOnlyList<ServerProjectInfo>> ListProjectsAsync(long userId, string? url, CancellationToken ct = default);

    // By id or case-insensitive name; the original becomes the base image and the project active.
    Task<UserSession> FetchAsync(long userId, string nameOrId, string? url, CancellationToken ct = default);

    // remoteSync.createRemoteProject: render, create, upload the rendered original.
    Task<ProjectRecord> CreateProjectAsync(long userId, string? name, string? url, CancellationToken ct = default);

    // remoteSync.saveRemoteProject: the active project's layout plus the rendered result.
    Task<ProjectRecord> SaveActiveProjectAsync(long userId, CancellationToken ct = default);

    // #rrggbb, or "" to clear. Returns the effective colour.
    Task<string> SetProjectColorAsync(long userId, string color, CancellationToken ct = default);

    Task<string> SetProjectNameAsync(long userId, string name, CancellationToken ct = default);

    // "" clears it. Returns the effective description.
    Task<string> SetProjectDescriptionAsync(long userId, string description, CancellationToken ct = default);

    // "" = not a blank image.
    Task<string> GetProjectBlankColorAsync(long userId, CancellationToken ct = default);

    Task<string> SetProjectBlankColorAsync(long userId, string color, CancellationToken ct = default);

    // Epoch ms, or 0 to keep the project forever. Returns the effective expiry.
    Task<long> SetProjectExpiryAsync(long userId, long expiresAtMs, CancellationToken ct = default);

    // The working image is kept for re-saving elsewhere; the server refuses while peers are in its
    // session.
    Task<string> DeleteActiveProjectAsync(long userId, CancellationToken ct = default);

    // A FRESH read; null without an active project or reachable server. The sync poller diffs it.
    Task<long?> ActiveServerVersionAsync(long userId, CancellationToken ct = default);

    // Pull a peer's change: reload the original and rebuild the edit state.
    Task<UserSession?> PullActiveAsync(long userId, CancellationToken ct = default);

    // Filestore-only per §9: never bumps the project version, so no version refresh follows.
    Task SaveChatAsync(long userId, string chatJson, CancellationToken ct = default);

    Task<string?> LoadChatAsync(long userId, CancellationToken ct = default);

    // The §9 per-file DELETE: idempotent, never bumps the version, a no-op without a project.
    Task DeleteChatAsync(long userId, CancellationToken ct = default);
}
