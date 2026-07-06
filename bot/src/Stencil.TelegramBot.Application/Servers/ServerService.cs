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
public sealed class ServerService : IServerService
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

    /// <inheritdoc />
    public async Task<ServerConnectionInfo> ConnectAsync(long userId, string url, string? token, bool verifyTls, CancellationToken ct = default)
    {
        // The bot is open to any Telegram user, so vet the target before issuing any REST call:
        // localhost/LAN collaboration servers are intended, but link-local / cloud-metadata
        // (169.254.169.254, fe80::/10, …) hosts are an SSRF-only target and are rejected.
        await RemoteImageUrl.ValidateServerUrlAsync(url, ct);
        var session = await _store.GetAsync(userId, ct);
        var client = _factory.Create(url, token, verifyTls);
        var effectiveToken = await client.ConnectAsync(token, ct);
        var normalized = _factory.NormalizeUrl(url);
        var info = new ServerConnectionInfo
        {
            Url = normalized,
            Token = effectiveToken,
            VerifyTls = verifyTls,
        };
        var connections = session.Connections
            .Where(c => c.Url != normalized)
            .Append(info)
            .ToList();
        var updated = session with { Connections = connections };
        await _store.SaveAsync(updated, ct);
        return info;
    }

    /// <inheritdoc />
    public async Task<bool> DisconnectAsync(long userId, string? url, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var connections = session.Connections.ToList();
        ServerConnectionInfo? removed;
        if (url is null)
        {
            removed = connections.Count == 0 ? null : connections[^1];
        }
        else
        {
            var normalized = _factory.NormalizeUrl(url);
            removed = session.FindConnection(normalized);
        }
        if (removed is null)
        {
            return false;
        }
        connections.Remove(removed);
        var updated = session with { Connections = connections };
        await _store.SaveAsync(updated, ct);
        return true;
    }

    /// <inheritdoc />
    public async Task<IReadOnlyList<ServerConnectionInfo>> ConnectionsAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        return session.Connections;
    }

    /// <inheritdoc />
    public async Task<IReadOnlyList<ServerProjectInfo>> ListProjectsAsync(long userId, string? url, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var targets = TargetConnections(session, url);
        var projects = new List<ServerProjectInfo>();
        foreach (var connection in targets)
        {
            var client = ClientFor(connection);
            try
            {
                var records = await client.ListProjectsAsync(ct);
                foreach (var record in records)
                {
                    projects.Add(new ServerProjectInfo(record, connection.Url));
                }
            }
            catch
            {
                // Skip an unreachable/erroring server, like the browser/pystencil does.
            }
        }
        return projects;
    }

    /// <inheritdoc />
    public async Task<UserSession> FetchAsync(long userId, string nameOrId, string? url, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var targets = TargetConnections(session, url);
        foreach (var connection in targets)
        {
            var client = ClientFor(connection);
            ProjectRecord? match;
            try
            {
                var records = await client.ListProjectsAsync(ct);
                match = records.FirstOrDefault(r =>
                    r.Id == nameOrId ||
                    string.Equals(r.Name, nameOrId, StringComparison.OrdinalIgnoreCase));
            }
            catch
            {
                // Skip an unreachable/erroring server and keep looking.
                continue;
            }
            if (match is null)
            {
                continue;
            }
            var full = await client.GetProjectAsync(match.Id, ct);
            var bytes = await client.GetFileAsync(match.Id, ProjectFileKind.Original, ct);
            var path = await _editing.StoreOriginalBytesAsync(userId, bytes, ".png", ct);
            // Rebuild the project's edit state (lines + filter + rotation + crop) from its layout
            // so re-rendering the original reproduces the same result every other client shows.
            var edits = full.Layout is JsonElement layout
                ? ProjectLayoutMapper.ToEditState(layout, full.Project.ImageW, full.Project.ImageH)
                : new EditState();
            var updated = session with
            {
                OriginalImagePath = path,
                OriginalWidth = full.Project.ImageW,
                OriginalHeight = full.Project.ImageH,
                ImageLabel = full.Project.Name,
                Edits = edits,
                EditHistory = [],
                EditRedo = [],
                ActiveServerUrl = connection.Url,
                ActiveProjectId = full.Project.Id,
                ActiveProjectName = full.Project.Name,
                ActiveProjectCreatedAt = full.Project.CreatedAt,
                ActiveProjectExpiresAt = full.Project.ExpiresAt,
                ActiveProjectVersion = full.Project.Version,
                ActiveProjectLayoutJson = full.Layout?.GetRawText(),
            };
            await _store.SaveAsync(updated, ct);
            return updated;
        }
        throw new InvalidOperationException($"Project '{nameOrId}' not found");
    }

    /// <inheritdoc />
    public async Task<ProjectRecord> CreateProjectAsync(long userId, string? name, string? url, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (!session.HasImage)
        {
            throw new InvalidOperationException("No working image — upload a photo or use /blank first.");
        }
        var connection = ResolveConnection(session, url);
        var client = ClientFor(connection);
        var render = await _editing.RenderAsync(userId, ct);
        var bytes = await File.ReadAllBytesAsync(render.Path, ct);
        var request = new CreateProjectRequest
        {
            Name = name ?? session.ImageLabel ?? "Untitled",
            HasImage = true,
            ImageW = render.Width,
            ImageH = render.Height,
        };
        var record = await client.CreateProjectAsync(request, ct);
        await client.PutFileAsync(record.Id, ProjectFileKind.Original, bytes, "png", render.Width, render.Height, ct);
        // The original upload bumps the server-side version but the file-write response carries
        // none, so re-read it — otherwise the session tracks a stale version and the very next
        // version-guarded save/colour/expiry would 409 (remoteSync.js createRemoteProject).
        var version = await CurrentVersionAsync(client, record.Id, record.Version, ct);
        var updated = session with
        {
            ActiveServerUrl = connection.Url,
            ActiveProjectId = record.Id,
            ActiveProjectName = record.Name,
            ActiveProjectCreatedAt = record.CreatedAt,
            ActiveProjectExpiresAt = record.ExpiresAt,
            ActiveProjectVersion = version,
            ActiveProjectLayoutJson = null, // bot-created: no prior layout to preserve
        };
        await _store.SaveAsync(updated, ct);
        return record with { Version = version };
    }

    /// <inheritdoc />
    public async Task<ProjectRecord> SaveActiveProjectAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null || session.ActiveServerUrl is null)
        {
            throw new InvalidOperationException("No active server project — /fetch or /create one first.");
        }
        var client = ClientForActive(session);
        var render = await _editing.RenderAsync(userId, ct);
        var bytes = await File.ReadAllBytesAsync(render.Path, ct);
        // Merge the current edit state into the project's existing layout so crop/page/formula
        // fields survive while lines/filter/rotation are updated (see ProjectLayoutWriter).
        var layoutJson = ProjectLayoutWriter.BuildJson(session.ActiveProjectLayoutJson, session.Edits, render.Width, render.Height);
        var request = new UpdateProjectRequest
        {
            Layout = JsonSerializer.Deserialize<JsonElement>(layoutJson),
            Version = session.ActiveProjectVersion,
        };
        var record = await UpdateOrConflictAsync(
            client,
            session.ActiveProjectId,
            request,
            "This project was edited elsewhere — reload it from the server before saving again.",
            ct);
        await client.PutFileAsync(session.ActiveProjectId, ProjectFileKind.Result, bytes, "png", render.Width, render.Height, ct);
        // The result upload bumps the version too; re-read it so the next save isn't stale
        // (remoteSync.js saveRemoteProject refreshes version after putFile('result')).
        var version = await CurrentVersionAsync(client, session.ActiveProjectId, record.Version, ct);
        var updated = session with { ActiveProjectVersion = version, ActiveProjectLayoutJson = layoutJson };
        await _store.SaveAsync(updated, ct);
        return record with { Version = version };
    }

    /// <inheritdoc />
    public async Task<string> SetProjectColorAsync(long userId, string color, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null || session.ActiveServerUrl is null)
        {
            throw new InvalidOperationException("No active server project — /fetch or /create one first.");
        }
        var client = ClientForActive(session);
        var record = await UpdateFieldWithRetryAsync(
            client,
            session.ActiveProjectId,
            v => new UpdateProjectRequest { Color = color, Version = v },
            "This project was edited elsewhere — reload it before changing its colour.",
            ct);
        var updated = session with { ActiveProjectVersion = record.Version };
        await _store.SaveAsync(updated, ct);
        return record.Color ?? "";
    }

    /// <inheritdoc />
    public async Task<long> SetProjectExpiryAsync(long userId, long expiresAtMs, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null || session.ActiveServerUrl is null)
        {
            throw new InvalidOperationException("No active server project — /fetch or /create one first.");
        }
        var client = ClientForActive(session);
        // 0 means "keep forever": it is sent explicitly (not null) so the server clears any expiry.
        var record = await UpdateFieldWithRetryAsync(
            client,
            session.ActiveProjectId,
            v => new UpdateProjectRequest { ExpiresAt = expiresAtMs, Version = v },
            "This project was edited elsewhere — reload it before changing its expiry.",
            ct);
        var updated = session with { ActiveProjectVersion = record.Version, ActiveProjectExpiresAt = record.ExpiresAt };
        await _store.SaveAsync(updated, ct);
        return record.ExpiresAt;
    }

    /// <inheritdoc />
    public async Task<string> DeleteActiveProjectAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null || session.ActiveServerUrl is null)
        {
            throw new InvalidOperationException("No active server project — /fetch or /create one first.");
        }
        var client = ClientForActive(session);
        var name = session.ActiveProjectName ?? session.ActiveProjectId;
        try
        {
            await client.DeleteProjectAsync(session.ActiveProjectId, ct);
        }
        catch (ServerException ex) when (ex.IsConflict)
        {
            throw new ServerException(
                "conflict",
                "The project is open by other clients right now — it can't be deleted until they leave.",
                ex.Status);
        }
        // Clear the active project (and live sync, which now has nothing to track); the working
        // image stays so the user can re-save it as a new project elsewhere.
        var updated = session with
        {
            ActiveServerUrl = null,
            ActiveProjectId = null,
            ActiveProjectName = null,
            ActiveProjectCreatedAt = 0,
            ActiveProjectExpiresAt = 0,
            ActiveProjectVersion = 0,
            ActiveProjectLayoutJson = null,
            SyncEnabled = false,
        };
        await _store.SaveAsync(updated, ct);
        return name;
    }

    /// <inheritdoc />
    public async Task<long?> ActiveServerVersionAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null || session.ActiveServerUrl is null)
        {
            return null;
        }
        var client = ClientForActive(session);
        try
        {
            var full = await client.GetProjectAsync(session.ActiveProjectId, ct);
            return full.Project.Version;
        }
        catch
        {
            return null; // unreachable — the poller simply retries next tick
        }
    }

    /// <inheritdoc />
    public async Task<UserSession?> PullActiveAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null || session.ActiveServerUrl is null)
        {
            return null;
        }
        return await FetchAsync(userId, session.ActiveProjectId, session.ActiveServerUrl, ct);
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

    /// <summary>Build a client for a remembered connection, reusing its stored token + TLS choice.</summary>
    private IStencilServerClient ClientFor(ServerConnectionInfo connection) =>
        _factory.Create(connection.Url, connection.Token, connection.VerifyTls);

    /// <summary>
    /// A client for the active project's server: its remembered connection (with token/TLS), or a
    /// bare client on the stored origin. Callers must have already checked <c>ActiveServerUrl</c>.
    /// </summary>
    private IStencilServerClient ClientForActive(UserSession session)
    {
        var connection = session.FindConnection(session.ActiveServerUrl!);
        return connection is not null ? ClientFor(connection) : _factory.Create(session.ActiveServerUrl!);
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
