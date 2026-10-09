using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Editing;

public sealed partial class EditingService
{
    public async Task<RenderResult> RenderAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        return await renderWithAsync(userId, session, session.Edits, ct);
    }

    // The CLI's contour filter to a fresh PNG: the §7 edge-map attachment.
    public Task<RenderResult> RenderContourAsync(long userId, string sourcePath, CancellationToken ct = default) =>
        _cli.EditAsync(new EditRequest
        {
            Input = sourcePath,
            Filter = "contour",
            Output = _workspace.NewFilePath(userId, ".png"),
            Overwrite = true,
        }, ct);

    // The variant path: renders from a copy of the state, leaving the session's own state alone.
    public async Task<RenderResult> RenderAsync(long userId, EditState edits, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        return await renderWithAsync(userId, session, edits, ct);
    }

    private async Task<RenderResult> renderWithAsync(long userId, UserSession session, EditState edits, CancellationToken ct)
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
            Flip = edits.Flip,
            Filter = edits.Filter,
            LayoutPath = layoutPath,
            Output = _workspace.NewFilePath(userId, ".png"),
            Overwrite = true,
        };
        try
        {
            return await _cli.EditAsync(request, ct);
        }
        finally
        {
            if (layoutPath is not null)
            {
                _workspace.Discard(layoutPath);
            }
        }
    }

    // A render the caller has sent or uploaded; the janitor would only sweep it after WorkspaceTtl.
    public void Discard(RenderResult render) => _workspace.Discard(render.Path);

    public StencilLayout BuildLayout(UserSession session) =>
        new()
        {
            ImageWidth = session.OriginalWidth,
            ImageHeight = session.OriginalHeight,
            Filter = session.Edits.Filter,
            Lines = session.Edits.Layout?.Lines ?? [],
        };

    public string ExportLayoutJson(UserSession session) =>
        StencilJson.SerializeIndented(BuildLayout(session));

}
