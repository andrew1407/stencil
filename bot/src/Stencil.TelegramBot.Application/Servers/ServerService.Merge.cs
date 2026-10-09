using System.Text.Json;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Servers;

// The version-guarded layout save and its 409 merge-and-retry, a port of the browser's pushLayout
// (js/core/remote/push.js): the peer's lines come first, core's mergeLines keeps the local rest.
public sealed partial class ServerService
{
    // The browser's MAX_TRIES.
    private const int _layoutSaveTries = 6;

    private const string _saveConflictMessage =
        "This project was edited elsewhere — reload it from the server before saving again.";

    private async Task<LayoutSave> saveLayoutAsync(
        IStencilServerClient client, string projectId, UserSession session, RenderResult render, CancellationToken ct)
    {
        EditState edits = session.Edits;
        string? baseJson = session.ActiveProjectLayoutJson;
        long version = session.ActiveProjectVersion;
        IReadOnlyList<LayoutLine> seen = [];
        for (int attempt = 0; ; attempt++)
        {
            string layoutJson = ProjectLayoutWriter.BuildJson(baseJson, edits, render.Width, render.Height);
            var request = new UpdateProjectRequest
            {
                Layout = JsonSerializer.Deserialize<JsonElement>(layoutJson, StencilJson.Options),
                Version = version,
            };
            try
            {
                var record = await client.UpdateProjectAsync(projectId, request, ct);
                return new LayoutSave(record, layoutJson, edits, attempt > 0);
            }
            catch (ServerException ex) when (ex.IsConflict)
            {
                if (attempt + 1 >= _layoutSaveTries)
                {
                    throw new ServerException("conflict", _saveConflictMessage, ex.Status);
                }
            }
            // The peer's layout is the next base: its own fields survive, the bot's edits override.
            var full = await client.GetProjectAsync(projectId, ct);
            var peer = peerLayout(full);
            var merge = await _cli.MergeLinesAsync(peer?.Lines ?? [], edits.Layout?.Lines ?? [], seen, ct);
            seen = peer?.Lines ?? [];
            edits = edits with { Layout = withLines(edits.Layout ?? peer, merge.Lines) };
            baseJson = full.Layout?.GetRawText();
            version = full.Project.Version;
        }
    }

    private static StencilLayout? peerLayout(ProjectFull full) =>
        full.Layout is JsonElement layout
            ? ProjectLayoutMapper.ToEditState(layout, full.Project.ImageW, full.Project.ImageH).Layout
            : null;

    private static StencilLayout? withLines(StencilLayout? layout, IReadOnlyList<LayoutLine> lines) =>
        lines.Count == 0 ? null : (layout ?? new StencilLayout()) with { Lines = lines };

    private sealed record LayoutSave(ProjectRecord Record, string LayoutJson, EditState Edits, bool Merged);
}
