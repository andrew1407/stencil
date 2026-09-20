using System.Text.Json;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Servers;

public sealed partial class ServerService
{
    public async Task<IReadOnlyList<ServerProjectInfo>> ListProjectsAsync(long userId, string? url, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var targets = targetConnections(session, url);
        // Answers are stitched back in connection order, not reply order.
        var answers = await Task.WhenAll(targets.Select(connection => listOneAsync(connection, ct)));
        return [.. answers.SelectMany(a => a)];
    }

    private async Task<IReadOnlyList<ServerProjectInfo>> listOneAsync(ServerConnectionInfo connection, CancellationToken ct)
    {
        try
        {
            var records = await clientFor(connection).ListProjectsAsync(ct);
            return [.. records.Select(record => new ServerProjectInfo(record, connection.Url))];
        }
        catch
        {
            return [];
        }
    }

    public async Task<UserSession> FetchAsync(long userId, string nameOrId, string? url, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var targets = targetConnections(session, url);
        foreach (var connection in targets)
        {
            var client = clientFor(connection);
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
                continue;
            }
            if (match is null)
            {
                continue;
            }
            var full = await client.GetProjectAsync(match.Id, ct);
            var bytes = await client.GetFileAsync(match.Id, ProjectFileKind.ORIGINAL, ct);
            var path = await _editing.StoreOriginalBytesAsync(userId, bytes, ".png", ct);
            // Rebuild the edit state from the layout so re-rendering reproduces what other clients
            // show.
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
                ActiveProjectDescription = full.Project.Description ?? "",
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
        // The original upload bumps the version but the file-write response carries none: re-read
        // it or the next version-guarded write would 409 (remoteSync.js createRemoteProject).
        var version = await currentVersionAsync(client, record.Id, record.Version, ct);
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
