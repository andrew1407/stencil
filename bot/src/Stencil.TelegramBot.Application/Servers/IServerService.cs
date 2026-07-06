using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Servers;

/// <summary>
/// The per-user collaboration-server surface: connect/disconnect servers, list/fetch
/// projects across them, and create/save the active project. A faithful port of
/// <c>pystencil</c>'s <c>ConnectionManager</c> + <c>remoteSync</c> (REST only, no live feed),
/// driven by <see cref="IEditingService"/> for the pixel bytes.
/// </summary>
public interface IServerService
{
    /// <summary>
    /// Validate/acquire a token for <paramref name="url"/> and remember the connection on the
    /// session (deduped by normalised URL). Returns the stored connection info.
    /// </summary>
    Task<ServerConnectionInfo> ConnectAsync(long userId, string url, string? token, bool verifyTls, CancellationToken ct = default);

    /// <summary>
    /// Forget a connection (the named one, or the most recently added when omitted). Returns
    /// whether a connection was removed.
    /// </summary>
    Task<bool> DisconnectAsync(long userId, string? url, CancellationToken ct = default);

    /// <summary>The user's remembered connections.</summary>
    Task<IReadOnlyList<ServerConnectionInfo>> ConnectionsAsync(long userId, CancellationToken ct = default);

    /// <summary>
    /// List projects for one connection (when <paramref name="url"/> is given) or every
    /// connection, tagging each with its server origin. Unreachable servers are skipped.
    /// </summary>
    Task<IReadOnlyList<ServerProjectInfo>> ListProjectsAsync(long userId, string? url, CancellationToken ct = default);

    /// <summary>
    /// Find a project by id or (case-insensitive) name across the named/all connections,
    /// download its original as the new base image, and mark it the active project. Throws
    /// when no match exists.
    /// </summary>
    Task<UserSession> FetchAsync(long userId, string nameOrId, string? url, CancellationToken ct = default);

    /// <summary>
    /// Render the current result, create a project on the target connection and upload the
    /// rendered original. Mirrors <c>remoteSync.createRemoteProject</c>.
    /// </summary>
    Task<ProjectRecord> CreateProjectAsync(long userId, string? name, string? url, CancellationToken ct = default);

    /// <summary>
    /// Version-guarded save-back of the active project's layout plus the rendered result.
    /// Mirrors <c>remoteSync.saveRemoteProject</c>. Throws on a last-writer-wins conflict.
    /// </summary>
    Task<ProjectRecord> SaveActiveProjectAsync(long userId, CancellationToken ct = default);

    /// <summary>
    /// Set the active project's accent colour (a <c>#rrggbb</c> hex, or <c>""</c> to clear it),
    /// version-guarded, and broadcast to peers. Returns the effective colour. Requires an active
    /// project. Mirrors the CLI's <c>/project-color</c>.
    /// </summary>
    Task<string> SetProjectColorAsync(long userId, string color, CancellationToken ct = default);

    /// <summary>
    /// Set the active project's expiry (<paramref name="expiresAtMs"/> epoch ms, or <c>0</c> to
    /// clear it so the project is kept forever), version-guarded. Returns the effective expiry the
    /// server stored. Requires an active project; throws on a last-writer-wins conflict.
    /// </summary>
    Task<long> SetProjectExpiryAsync(long userId, long expiresAtMs, CancellationToken ct = default);

    /// <summary>
    /// Delete the active project from its server (<c>DELETE /projects/{id}</c>) and clear it from
    /// the session (the working image is kept for re-saving elsewhere). Returns the removed
    /// project's name. Requires an active project; the server refuses (conflict) while other
    /// clients are in its live edit session.
    /// </summary>
    Task<string> DeleteActiveProjectAsync(long userId, CancellationToken ct = default);

    /// <summary>
    /// The active project's current version on the server (a fresh read), or null when there is
    /// no active project or the server is unreachable. Used by the sync poller to detect a peer's
    /// change (server version &gt; the session's last-seen version).
    /// </summary>
    Task<long?> ActiveServerVersionAsync(long userId, CancellationToken ct = default);

    /// <summary>
    /// Re-fetch the active project by id (pull a peer's change): reload its original and rebuild
    /// the edit state, updating the session. Returns the refreshed session, or null when there is
    /// no active project.
    /// </summary>
    Task<UserSession?> PullActiveAsync(long userId, CancellationToken ct = default);
}
