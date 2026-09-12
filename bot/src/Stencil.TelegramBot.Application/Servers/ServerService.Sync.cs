using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Servers;

public sealed partial class ServerService
{
    public async Task<ProjectRecord> SaveActiveProjectAsync(long userId, CancellationToken ct = default)
    {
        var (session, projectId) = await requireActiveSessionAsync(userId, ct);
        var client = clientForActive(session);
        var render = await _editing.RenderAsync(userId, ct);
        var bytes = await File.ReadAllBytesAsync(render.Path, ct);
        // Merge into the existing layout so crop/page/formula fields survive (ProjectLayoutWriter).
        var layoutJson = ProjectLayoutWriter.BuildJson(session.ActiveProjectLayoutJson, session.Edits, render.Width, render.Height);
        var request = new UpdateProjectRequest
        {
            Layout = JsonSerializer.Deserialize<JsonElement>(layoutJson),
            Version = session.ActiveProjectVersion,
        };
        var record = await updateOrConflictAsync(
            client,
            projectId,
            request,
            "This project was edited elsewhere — reload it from the server before saving again.",
            ct);
        await client.PutFileAsync(projectId, ProjectFileKind.Result, bytes, "png", render.Width, render.Height, ct);
        // The result upload bumps the version too; re-read it (remoteSync.js saveRemoteProject).
        var version = await currentVersionAsync(client, projectId, record.Version, ct);
        var updated = session with { ActiveProjectVersion = version, ActiveProjectLayoutJson = layoutJson };
        await _store.SaveAsync(updated, ct);
        return record with { Version = version };
    }
    public async Task<long?> ActiveServerVersionAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null || session.ActiveServerUrl is null)
        {
            return null;
        }
        var client = clientForActive(session);
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

    public async Task<UserSession?> PullActiveAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null || session.ActiveServerUrl is null)
        {
            return null;
        }
        return await FetchAsync(userId, session.ActiveProjectId, session.ActiveServerUrl, ct);
    }

    public async Task SaveChatAsync(long userId, string chatJson, CancellationToken ct = default)
    {
        var (session, projectId) = await requireActiveSessionAsync(userId, ct);
        var client = clientForActive(session);
        // §9: chat is filestore-only — the upload does NOT bump the version, so no re-read is
        // needed.
        await client.PutFileAsync(projectId, ProjectFileKind.Chat,
            Encoding.UTF8.GetBytes(chatJson), "json", 0, 0, ct);
    }

    public async Task<string?> LoadChatAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null || session.ActiveServerUrl is null)
        {
            return null;
        }
        var client = clientForActive(session);
        try
        {
            var bytes = await client.GetFileAsync(session.ActiveProjectId, ProjectFileKind.Chat, ct);
            return Encoding.UTF8.GetString(bytes);
        }
        catch (ServerException ex) when (ex.Status == 404)
        {
            return null; // no chat saved with this project
        }
    }

    public async Task DeleteChatAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null || session.ActiveServerUrl is null)
        {
            return;
        }
        var client = clientForActive(session);
        // Idempotent per §9 (an absent chat still answers 204); never bumps the version.
        await client.DeleteFileAsync(session.ActiveProjectId, ProjectFileKind.Chat, ct);
    }
}
