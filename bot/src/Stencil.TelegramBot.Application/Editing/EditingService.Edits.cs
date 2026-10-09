using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Editing;

public sealed partial class EditingService
{
    public Task<UserSession> SetCropAsync(long userId, string spec, bool album, CancellationToken ct = default) =>
        applyEditAsync(userId, edits => edits with { CropSpec = spec, Album = album }, ct);

    // A crop of the view the session renders, composed onto the stored one; the lines rescale, or
    // clear on an album/portrait flip (a plan crop, llm-contract §2).
    public Task<UserSession> ComposeCropAsync(long userId, string spec, CancellationToken ct = default) =>
        applyEditAsync(userId, session => session.Edits.WithViewCrop(spec, session.OriginalWidth, session.OriginalHeight), ct);

    // Accumulates clockwise, normalised to 0..3.
    public Task<UserSession> RotateAsync(long userId, int quarterTurns, CancellationToken ct = default) =>
        applyEditAsync(userId, edits => edits with { Rotate = ((((edits.Rotate + quarterTurns) % 4) + 4) % 4) }, ct);

    // Flipping the shown view mirrors it and negates the turn: turn(q)·mirror = mirror·turn(−q).
    public Task<UserSession> FlipAsync(long userId, CancellationToken ct = default) =>
        applyEditAsync(userId, edits => edits with { Flip = !edits.Flip, Rotate = (4 - edits.Rotate) % 4 }, ct);

    // Null/empty/"none" clears it.
    public Task<UserSession> SetFilterAsync(long userId, string? filter, CancellationToken ct = default) =>
        applyEditAsync(userId, edits => edits with { Filter = normalizeFilter(filter) }, ct);

    // A canonical ISO name (B5) or "custom" with cm; a named format is the /blank default page.
    public Task<UserSession> SetPageFormatAsync(long userId, string format, double? widthCm = null, double? heightCm = null, CancellationToken ct = default) =>
        applyEditAsync(userId, edits => withPageFormat(edits, format, widthCm, heightCm), ct);

    // cm dims only ride a custom format.
    private static EditState withPageFormat(EditState edits, string format, double? widthCm, double? heightCm) =>
        edits with
        {
            PageFormat = format,
            CustomPageWidth = format == "custom" ? widthCm : null,
            CustomPageHeight = format == "custom" ? heightCm : null,
        };

    // combine appends to the lines already drawn; false replaces them (the editors'
    // Combine/Replace).
    public Task<UserSession> ApplyLayoutAsync(
        long userId, StencilLayout layout, bool combine = false, CancellationToken ct = default) =>
        applyEditAsync(userId, edits =>
        {
            // Combine keeps what is drawn and puts the incoming lines on top, like the editors and
            // the CLI console.
            if (!combine || edits.Layout is null) return edits with { Layout = layout };
            List<LayoutLine> merged = [.. edits.Layout.Lines, .. layout.Lines];
            return edits with { Layout = layout with { Lines = merged } };
        }, ct);

    // Metadata like the browser's formulaX/Y: never changes the raster; an empty expr clears the
    // axis.
    public Task<UserSession> SetFormulaAsync(long userId, string axis, string expr, CancellationToken ct = default) =>
        applyEditAsync(userId, edits =>
        {
            string? value = string.IsNullOrWhiteSpace(expr) ? null : expr;
            return axis.Equals("y", StringComparison.OrdinalIgnoreCase)
                ? edits with { FormulaY = value }
                : edits with { FormulaX = value };
        }, ct);

    public async Task<UserSession> UndoAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var history = EditSessions.History(session);
        if (!history.CanUndo)
        {
            return session;
        }
        var (stepped, previous) = history.Undo(session.Edits);
        return await saveAsync(EditSessions.With(session, stepped, previous), ct);
    }

    public async Task<UserSession> RedoAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var history = EditSessions.History(session);
        if (!history.CanRedo)
        {
            return session;
        }
        var (stepped, next) = history.Redo(session.Edits);
        return await saveAsync(EditSessions.With(session, stepped, next), ct);
    }

    // Keeps the working image.
    public async Task<UserSession> ResetEditsAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        return await saveAsync(
            EditSessions.With(session, HistoryStack<EditState>.Empty, new EditState()), ct);
    }

    // Drops the working image AND the active project, and wipes the workspace.
    public async Task<UserSession> DropImageAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        _workspace.Clear(userId);
        var updated = session with
        {
            OriginalImagePath = null,
            OriginalWidth = 0,
            OriginalHeight = 0,
            ImageLabel = null,
            VideoSourcePath = null,
            Edits = new EditState(),
            EditHistory = [],
            EditRedo = [],
            ActiveServerUrl = null,
            ActiveProjectId = null,
            ActiveProjectName = null,
            ActiveProjectDescription = null,
            ActiveProjectCreatedAt = 0,
            ActiveProjectExpiresAt = 0,
            ActiveProjectVersion = 0,
            ActiveProjectLayoutJson = null,
        };
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    private async Task<UserSession> applyEditAsync(long userId, Func<UserSession, EditState> mutate, CancellationToken ct)
    {
        var session = await _store.GetAsync(userId, ct);
        return await saveAsync(EditSessions.WithHistory(session, mutate(session)), ct);
    }

    private Task<UserSession> applyEditAsync(long userId, Func<EditState, EditState> mutate, CancellationToken ct) =>
        applyEditAsync(userId, session => mutate(session.Edits), ct);

    private async Task<UserSession> saveAsync(UserSession updated, CancellationToken ct)
    {
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    private static string? normalizeFilter(string? filter)
    {
        if (string.IsNullOrEmpty(filter) || string.Equals(filter, "none", StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }
        return filter;
    }
}
