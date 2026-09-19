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

// A port of pystencil's ConnectionManager + remoteSync onto the per-user session. Clients are not
// cached: each call rebuilds one from the session's stored connection, so the session is the truth.
public sealed partial class ServerService : IServerService
{
    // A port of pystencil's _FIELD_WRITE_RETRIES / the CLI's putProjectField loop.
    private const int _fieldWriteRetries = 4;

    private readonly IStencilServerClientFactory _factory;
    private readonly ISessionStore _store;
    private readonly IEditingService _editing;

    public ServerService(IStencilServerClientFactory factory, ISessionStore store, IEditingService editing)
    {
        _factory = factory;
        _store = store;
        _editing = editing;
    }

    private IReadOnlyList<ServerConnectionInfo> targetConnections(UserSession session, string? url)
    {
        if (url is null)
        {
            return session.Connections;
        }
        var normalized = _factory.NormalizeUrl(url);
        var connection = session.FindConnection(normalized);
        return connection is null ? [] : [connection];
    }

    private ServerConnectionInfo resolveConnection(UserSession session, string? url)
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

    // The stored credential re-mints a stale session token in place.
    private IStencilServerClient clientFor(ServerConnectionInfo connection) =>
        _factory.Create(connection.Url, connection.Token, connection.VerifyTls, connection.Credential,
            connection.CredentialKind);

    // Callers must have already checked ActiveServerUrl.
    private IStencilServerClient clientForActive(UserSession session)
    {
        var connection = session.FindConnection(session.ActiveServerUrl!);
        return connection is not null ? clientFor(connection) : _factory.Create(session.ActiveServerUrl!);
    }

    private async Task<(UserSession Session, string ProjectId)> requireActiveSessionAsync(long userId, CancellationToken ct)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null || session.ActiveServerUrl is null)
        {
            throw new InvalidOperationException("No active server project — /fetch or /create one first.");
        }
        return (session, session.ActiveProjectId);
    }

    private static async Task<ProjectRecord> updateOrConflictAsync(
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

    // The read-then-PUT isn't atomic: a peer advancing the version would 409 and drop the change, so
    // re-read and retry, as pystencil's _update_field_with_retry does.
    private static async Task<ProjectRecord> updateFieldWithRetryAsync(
        IStencilServerClient client, string id, Func<long, UpdateProjectRequest> build, string conflictMessage, CancellationToken ct)
    {
        ServerException? last = null;
        for (int attempt = 0; attempt < _fieldWriteRetries; attempt++)
        {
            long version = await currentVersionAsync(client, id, 0, ct);
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

    // A file write bumps the version but returns none; mirrors remoteSync.js currentVersion.
    private static async Task<long> currentVersionAsync(IStencilServerClient client, string id, long fallback, CancellationToken ct)
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
