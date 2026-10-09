using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Servers;

public sealed partial class ServerService
{
    public Task<IReadOnlyList<ServerProjectInfo>> ListProjectsAsync(long userId, string? url, CancellationToken ct = default) =>
        listEachAsync(userId, url, (client, token) => client.ListProjectsAsync(token), ct);

    public Task<IReadOnlyList<ServerProjectInfo>> ListRecentProjectsAsync(long userId, int perServer, CancellationToken ct = default) =>
        listEachAsync(userId, null, (client, token) => client.ListFirstProjectsAsync(perServer, token), ct);

    private async Task<IReadOnlyList<ServerProjectInfo>> listEachAsync(long userId, string? url,
        Func<IStencilServerClient, CancellationToken, Task<IReadOnlyList<ProjectRecord>>> list, CancellationToken ct)
    {
        var session = await _store.GetAsync(userId, ct);
        var targets = targetConnections(session, url);
        var clients = targets.Select(clientFor).ToList();
        // Answers are stitched back in connection order, not reply order.
        var answers = await Task.WhenAll(targets.Select((connection, i) => listOneAsync(connection, clients[i], list, ct)));
        foreach (var client in clients)
        {
            await keepTokenAsync(userId, client, ct);
        }
        return [.. answers.SelectMany(a => a)];
    }

    private static async Task<IReadOnlyList<ServerProjectInfo>> listOneAsync(ServerConnectionInfo connection,
        IStencilServerClient client, Func<IStencilServerClient, CancellationToken, Task<IReadOnlyList<ProjectRecord>>> list,
        CancellationToken ct)
    {
        try
        {
            var records = await list(client, ct);
            return [.. records.Select(record => new ServerProjectInfo(record, connection.Url))];
        }
        catch
        {
            return [];
        }
    }

    public async Task<ProjectRecord> CreateProjectAsync(long userId, string? name, string? url, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (!session.HasImage)
        {
            throw new InvalidOperationException("No working image — upload a photo or use /blank first.");
        }
        var connection = resolveConnection(session, url);
        var client = clientFor(connection);
        var render = await _editing.RenderAsync(userId, ct);
        var bytes = await File.ReadAllBytesAsync(render.Path, ct);
        // A locally-held description rides along; null lets the server apply its default.
        var description = string.IsNullOrEmpty(session.ActiveProjectDescription) ? null : session.ActiveProjectDescription;
        var request = new CreateProjectRequest
        {
            Name = name ?? session.ImageLabel ?? "Untitled",
            Description = description,
            HasImage = true,
            ImageW = render.Width,
            ImageH = render.Height,
        };
        var record = await client.CreateProjectAsync(request, ct);
        await client.PutFileAsync(record.Id, ProjectFileKind.ORIGINAL, bytes, "png", render.Width, render.Height, ct);
        _editing.Discard(render);
        // The original upload bumps the version but the file-write response carries none: re-read
        // it or the next version-guarded write would 409 (remoteSync.js createRemoteProject).
        var version = await versionAfterWriteAsync(client, record.Id, record.Version, ct);
        var updated = session with
        {
            ActiveServerUrl = connection.Url,
            ActiveProjectId = record.Id,
            ActiveProjectName = record.Name,
            ActiveProjectDescription = record.Description ?? description ?? "",
            ActiveProjectCreatedAt = record.CreatedAt,
            ActiveProjectExpiresAt = record.ExpiresAt,
            ActiveProjectVersion = version,
            ActiveProjectLayoutJson = null, // bot-created: no prior layout to preserve
        };
        await _store.SaveAsync(updated, ct);
        await keepTokenAsync(userId, client, ct);
        return record with { Version = version };
    }
    public async Task<string> DeleteActiveProjectAsync(long userId, CancellationToken ct = default)
    {
        var (session, projectId) = await requireActiveSessionAsync(userId, ct);
        var client = clientForActive(session);
        var name = session.ActiveProjectName ?? projectId;
        try
        {
            await client.DeleteProjectAsync(projectId, ct);
        }
        catch (ServerException ex) when (ex.IsConflict)
        {
            throw new ServerException(
                "conflict",
                "The project is open by other clients right now — it can't be deleted until they leave.",
                ex.Status);
        }
        // The working image stays so the user can re-save it elsewhere.
        var updated = session with
        {
            ActiveServerUrl = null,
            ActiveProjectId = null,
            ActiveProjectName = null,
            ActiveProjectDescription = null,
            ActiveProjectCreatedAt = 0,
            ActiveProjectExpiresAt = 0,
            ActiveProjectVersion = 0,
            ActiveProjectLayoutJson = null,
            SyncEnabled = false,
        };
        await _store.SaveAsync(updated, ct);
        return name;
    }
}
