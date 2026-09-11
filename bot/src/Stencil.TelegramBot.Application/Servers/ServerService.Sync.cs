using System.Text;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Servers;

// ServerService — pushing the layout up and pulling it back, plus the §9 chat file. Class doc lives in ServerService.cs.
public sealed partial class ServerService
{
    /// <inheritdoc />
    public async Task<ProjectRecord> SaveActiveProjectAsync(long userId, CancellationToken ct = default)
    {
        var session = await RequireActiveSessionAsync(userId, ct);
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

    /// <inheritdoc />
    public async Task SaveChatAsync(long userId, string chatJson, CancellationToken ct = default)
    {
        var session = await RequireActiveSessionAsync(userId, ct);
        var client = ClientForActive(session);
        // Contract §9: `chat` is filestore-only — the upload does NOT bump the project version
        // (the server only bumps for original/result), so no version re-read/save is needed.
        await client.PutFileAsync(session.ActiveProjectId!, ProjectFileKind.Chat,
            Encoding.UTF8.GetBytes(chatJson), "json", 0, 0, ct);
    }

    /// <inheritdoc />
    public async Task<string?> LoadChatAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null || session.ActiveServerUrl is null)
        {
            return null;
        }
        var client = ClientForActive(session);
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

    /// <inheritdoc />
    public async Task DeleteChatAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.ActiveProjectId is null || session.ActiveServerUrl is null)
        {
            return;
        }
        var client = ClientForActive(session);
        // Idempotent per §9 (an absent chat still answers 204); never bumps the version.
        await client.DeleteFileAsync(session.ActiveProjectId, ProjectFileKind.Chat, ct);
    }
}
