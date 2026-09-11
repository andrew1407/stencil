using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Editing;

// EditingService — replaying the original through the CLI, plus the layout export both the
// /json command and a project save read. Class doc lives in EditingService.cs.
public sealed partial class EditingService
{
    /// <inheritdoc />
    public async Task<RenderResult> RenderAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        return await RenderWithAsync(userId, session, session.Edits, ct);
    }

    /// <inheritdoc />
    public Task<RenderResult> RenderContourAsync(long userId, string sourcePath, CancellationToken ct = default) =>
        _cli.EditAsync(new EditRequest
        {
            Input = sourcePath,
            Filter = "contour",
            Output = _workspace.NewFilePath(userId, ".png"),
            Overwrite = true,
        }, ct);

    /// <inheritdoc />
    public async Task<RenderResult> RenderAsync(long userId, EditState edits, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        return await RenderWithAsync(userId, session, edits, ct);
    }

    /// <summary>Replay the session's original through the CLI with the given edit state.</summary>
    private async Task<RenderResult> RenderWithAsync(long userId, UserSession session, EditState edits, CancellationToken ct)
    {
        if (session.OriginalImagePath is null)
        {
            throw new InvalidOperationException("No working image — upload a photo or use /blank first.");
        }
        string? layoutPath = null;
        if (edits.Layout is not null)
        {
            var json = StencilJson.Serialize(edits.Layout);
            var bytes = System.Text.Encoding.UTF8.GetBytes(json);
            layoutPath = await _workspace.WriteAsync(userId, bytes, ".json", ct);
        }
        var request = new EditRequest
        {
            Input = session.OriginalImagePath,
            CropSpec = edits.CropSpec,
            Album = edits.Album,
            Rotate = edits.Rotate == 0 ? null : edits.Rotate,
            Filter = edits.Filter,
            LayoutPath = layoutPath,
            Output = _workspace.NewFilePath(userId, ".png"),
            Overwrite = true,
        };
        return await _cli.EditAsync(request, ct);
    }

    /// <inheritdoc />
    public StencilLayout BuildLayout(UserSession session) =>
        new()
        {
            ImageWidth = session.OriginalWidth,
            ImageHeight = session.OriginalHeight,
            Filter = session.Edits.Filter,
            Lines = session.Edits.Layout?.Lines ?? [],
        };

    /// <inheritdoc />
    public string ExportLayoutJson(UserSession session) =>
        StencilJson.SerializeIndented(BuildLayout(session));

}
