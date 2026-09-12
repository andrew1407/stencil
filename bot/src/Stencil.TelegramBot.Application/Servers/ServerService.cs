using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Servers;

/// <summary>
/// Default <see cref="IServerService"/>: a port of <c>pystencil</c>'s <c>ConnectionManager</c>
/// + <c>remoteSync</c> (server.py) onto the bot's per-user session model. REST only — a
/// connection is a validated token + base URL, persisted on the <see cref="UserSession"/>.
/// </summary>
/// <remarks>
/// Clients are not cached: each call rebuilds a <see cref="IStencilServerClient"/> from the
/// session's stored <see cref="ServerConnectionInfo"/> via the factory, so the service stays
/// stateless and the session remains the single source of truth.
/// </remarks>
public sealed partial class ServerService : IServerService
{
    /// <summary>
    /// Attempts for a version-guarded single-field write before giving up on a sustained conflict
    /// (a port of pystencil's <c>_FIELD_WRITE_RETRIES</c> / the CLI's <c>putProjectField</c> loop).
    /// </summary>
    private const int FieldWriteRetries = 4;

    private readonly IStencilServerClientFactory _factory;
    private readonly ISessionStore _store;
    private readonly IEditingService _editing;

    public ServerService(IStencilServerClientFactory factory, ISessionStore store, IEditingService editing)
    {
        _factory = factory;
        _store = store;
        _editing = editing;
    }

    /// <summary>The connections to query: the named one (normalised) or every connection.</summary>
    private IReadOnlyList<ServerConnectionInfo> TargetConnections(UserSession session, string? url)
    {
        if (url is null)
        {
            return session.Connections;
        }
        var normalized = _factory.NormalizeUrl(url);
        var connection = session.FindConnection(normalized);
        return connection is null ? [] : [connection];
    }

    /// <summary>
    /// Resolve a create/save target: the named connection, else the single/last one, else
    /// a friendly "connect first" error.
    /// </summary>
    private ServerConnectionInfo ResolveConnection(UserSession session, string? url)
    {
        if (url is not null)
        {
            var normalized = _factory.NormalizeUrl(url);
            var named = session.FindConnection(normalized);
            if (named is null)
            {
                throw new InvalidOperationException("Connect to a server first.");
            }
            return named;
        }
        if (session.Connections.Count == 0)
        {
            throw new InvalidOperationException("Connect to a server first.");
        }
        return session.Connections[^1];
    }

    /// <summary>Build a client for a remembered connection, reusing its stored token +
    /// credential (+ kind) + TLS choice (the credential re-mints a stale session token in place).</summary>
    private IStencilServerClient ClientFor(ServerConnectionInfo connection) =>
        _factory.Create(connection.Url, connection.Token, connection.VerifyTls, connection.Credential,
            connection.CredentialKind);

    /// <summary>
    /// A client for the active project's server: its remembered connection (with token/TLS), or a
    /// bare client on the stored origin. Callers must have already checked <c>ActiveServerUrl</c>.
    /// </summary>
    private IStencilServerClient ClientForActive(UserSession session)
    {
        var connection = session.FindConnection(session.ActiveServerUrl!);
        return connection is not null ? ClientFor(connection) : _factory.Create(session.ActiveServerUrl!);
    }

    /// <summary>Load the session and assert it has an active server project, or throw (pollers
    /// return null instead); the id returns separately so non-null reaches callers as a type.</summary>
    private async Task<(UserSession Session, string ProjectId)> RequireActiveSessionAsync(long userId, CancellationToken ct)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null || session.ActiveServerUrl is null)
        {
            throw new InvalidOperationException("No active server project — /fetch or /create one first.");
        }
        return (session, session.ActiveProjectId);
    }

    /// <summary>Update a project, translating a version conflict into a friendly reload prompt.</summary>
    private static async Task<ProjectRecord> UpdateOrConflictAsync(
        IStencilServerClient client, string id, UpdateProjectRequest request, string conflictMessage, CancellationToken ct)
    {
        try
        {
            return await client.UpdateProjectAsync(id, request, ct);
        }
        catch (ServerException ex) when (ex.IsConflict)
        {
            throw new ServerException("conflict", conflictMessage, ex.Status);
        }
    }

    /// <summary>
    /// A version-guarded single-field write (colour / expiry) with a bounded conflict retry: the
    /// read-then-PUT isn't atomic, so a peer — or our own preceding file upload — that advanced the
    /// version between the read and the PUT would 409 and silently drop the change. On a conflict we
    /// re-read the current version and retry, mirroring pystencil's <c>_update_field_with_retry</c>
    /// / the CLI's <c>putProjectField</c>. A sustained conflict surfaces the friendly reload prompt.
    /// </summary>
    private static async Task<ProjectRecord> UpdateFieldWithRetryAsync(
        IStencilServerClient client, string id, Func<long, UpdateProjectRequest> build, string conflictMessage, CancellationToken ct)
    {
        ServerException? last = null;
        for (int attempt = 0; attempt < FieldWriteRetries; attempt++)
        {
            long version = await CurrentVersionAsync(client, id, 0, ct);
            try
            {
                return await client.UpdateProjectAsync(id, build(version), ct);
            }
            catch (ServerException ex) when (ex.IsConflict)
            {
                last = ex; // a peer won the race — re-read the version and retry
            }
        }
        throw new ServerException("conflict", conflictMessage, last?.Status ?? 409);
    }

    /// <summary>
    /// Re-read a project's current version (a file write bumps it but returns none of its own),
    /// falling back to <paramref name="fallback"/> when the server is unreachable. Mirrors
    /// remoteSync.js <c>currentVersion</c> / pystencil <c>_current_version</c>.
    /// </summary>
    private static async Task<long> CurrentVersionAsync(IStencilServerClient client, string id, long fallback, CancellationToken ct)
    {
        try
        {
            var full = await client.GetProjectAsync(id, ct);
            return full.Project.Version;
        }
        catch
        {
            return fallback;
        }
    }
}
