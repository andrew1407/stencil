using System.Text;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Servers;

public sealed partial class ServerService
{
    // `rendered` is a render of the session's current state the caller already made; it stays the caller's.
    public async Task<ProjectRecord> SaveActiveProjectAsync(long userId, RenderResult? rendered = null, CancellationToken ct = default)
    {
        var (session, projectId) = await requireActiveSessionAsync(userId, ct);
        var client = clientForActive(session);
        var render = rendered ?? await _editing.RenderAsync(userId, ct);
        try
        {
            var saved = await saveLayoutAsync(client, projectId, session, render, ct);
            // The layout write's version names the merged layout, so both are adopted together.
            session = session with
            {
                Edits = saved.Edits,
                ActiveProjectVersion = saved.Record.Version,
                ActiveProjectLayoutJson = saved.LayoutJson,
            };
            await _store.SaveAsync(session, ct);
            if (saved.Merged)
            {
                discardOwn(render, rendered);
                render = await _editing.RenderAsync(userId, saved.Edits, ct); // the result shows the peer's lines too
            }
            var bytes = await File.ReadAllBytesAsync(render.Path, ct);
            await client.PutFileAsync(projectId, ProjectFileKind.RESULT, bytes, "png", render.Width, render.Height, ct);
            var version = await versionAfterWriteAsync(client, projectId, saved.Record.Version, ct);
            await _store.SaveAsync(session with { ActiveProjectVersion = version }, ct);
            await keepTokenAsync(userId, client, ct);
            return saved.Record with { Version = version };
        }
        finally
        {
            discardOwn(render, rendered);
        }
    }

    private void discardOwn(RenderResult render, RenderResult? callers)
    {
        if (render != callers)
        {
            _editing.Discard(render);
        }
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
            await keepTokenAsync(userId, client, ct);
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
        await client.PutFileAsync(projectId, ProjectFileKind.CHAT,
            Encoding.UTF8.GetBytes(chatJson), "json", 0, 0, ct);
        await keepTokenAsync(userId, client, ct);
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
            var bytes = await client.GetFileAsync(session.ActiveProjectId, ProjectFileKind.CHAT, ct);
            await keepTokenAsync(userId, client, ct);
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
        await client.DeleteFileAsync(session.ActiveProjectId, ProjectFileKind.CHAT, ct);
        await keepTokenAsync(userId, client, ct);
    }
}
