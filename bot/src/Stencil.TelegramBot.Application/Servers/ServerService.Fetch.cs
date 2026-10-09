using System.Text.Json;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Servers;

// Opening a server project into the session: its original image and the EditState its layout names.
public sealed partial class ServerService
{
    public async Task<UserSession> FetchAsync(long userId, string nameOrId, string? url, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        foreach (var connection in targetConnections(session, url))
        {
            var client = clientFor(connection);
            if (await findAsync(client, nameOrId, ct) is not ProjectFull full)
            {
                continue;
            }
            var project = full.Project;
            // The sync poll pulls every peer save; an unchanged original is not downloaded again.
            bool sameOriginal = !string.IsNullOrEmpty(project.OriginalHash)
                && session.ActiveProjectId == project.Id
                && session.ActiveProjectOriginalHash == project.OriginalHash
                && session.OriginalImagePath is string kept && File.Exists(kept);
            var path = sameOriginal
                ? session.OriginalImagePath!
                : await _editing.StoreOriginalBytesAsync(
                    userId, await client.GetFileAsync(project.Id, ProjectFileKind.ORIGINAL, ct), ".png", ct);
            // Rebuild the edit state from the layout so re-rendering reproduces what other clients
            // show.
            var edits = full.Layout is JsonElement layout
                ? ProjectLayoutMapper.ToEditState(layout, project.ImageW, project.ImageH)
                : new EditState();
            var updated = session with
            {
                OriginalImagePath = path,
                OriginalWidth = project.ImageW,
                OriginalHeight = project.ImageH,
                ImageLabel = project.Name,
                Edits = edits,
                EditHistory = [],
                EditRedo = [],
                ActiveServerUrl = connection.Url,
                ActiveProjectId = project.Id,
                ActiveProjectName = project.Name,
                ActiveProjectDescription = project.Description ?? "",
                ActiveProjectCreatedAt = project.CreatedAt,
                ActiveProjectExpiresAt = project.ExpiresAt,
                ActiveProjectVersion = project.Version,
                ActiveProjectLayoutJson = full.Layout?.GetRawText(),
                ActiveProjectOriginalHash = project.OriginalHash,
            };
            await _store.SaveAsync(updated, ct);
            await keepTokenAsync(userId, client, ct);
            return await _store.GetAsync(userId, ct);
        }
        throw new InvalidOperationException($"Project '{nameOrId}' not found");
    }

    // An id is one GET; a name the server refuses as an id walks the listing. Null when this server
    // is unreachable or has no such project.
    private static async Task<ProjectFull?> findAsync(IStencilServerClient client, string nameOrId, CancellationToken ct)
    {
        try
        {
            return await client.GetProjectAsync(nameOrId, ct);
        }
        catch (Exception ex) when (ex is not OperationCanceledException && !refusedAsId(ex))
        {
            return null;
        }
        catch (ServerException)
        {
        }
        ProjectRecord? match;
        try
        {
            var records = await client.ListProjectsAsync(ct);
            match = records.FirstOrDefault(r => string.Equals(r.Name, nameOrId, StringComparison.OrdinalIgnoreCase));
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            return null;
        }
        return match is null ? null : await client.GetProjectAsync(match.Id, ct);
    }

    private static bool refusedAsId(Exception ex) => ex is ServerException { Status: >= 400 and < 500 };
}
