using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Servers;

// ServerService — project CRUD: list, fetch onto the session, create from the current render, delete. Class doc lives in ServerService.cs.
public sealed partial class ServerService
{
    /// <inheritdoc />
    public async Task<IReadOnlyList<ServerProjectInfo>> ListProjectsAsync(long userId, string? url, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var targets = TargetConnections(session, url);
        // Independent servers, so ask them all at once; the answers are stitched back in
        // connection order, not reply order.
        var answers = await Task.WhenAll(targets.Select(connection => ListOneAsync(connection, ct)));
        return [.. answers.SelectMany(a => a)];
    }

    private async Task<IReadOnlyList<ServerProjectInfo>> ListOneAsync(ServerConnectionInfo connection, CancellationToken ct)
    {
        try
        {
            var records = await ClientFor(connection).ListProjectsAsync(ct);
            return [.. records.Select(record => new ServerProjectInfo(record, connection.Url))];
        }
        catch
        {
            return [];
        }
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
        // Carry any locally-held description (set via /project-description before saving) so the
        // new project keeps it; null when none so the server applies its default (no description).
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
            ActiveProjectDescription = record.Description ?? description ?? "",
            ActiveProjectCreatedAt = record.CreatedAt,
            ActiveProjectExpiresAt = record.ExpiresAt,
            ActiveProjectVersion = version,
            ActiveProjectLayoutJson = null, // bot-created: no prior layout to preserve
        };
        await _store.SaveAsync(updated, ct);
        return record with { Version = version };
    }
    /// <inheritdoc />
    public async Task<string> DeleteActiveProjectAsync(long userId, CancellationToken ct = default)
    {
        var session = await RequireActiveSessionAsync(userId, ct);
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
