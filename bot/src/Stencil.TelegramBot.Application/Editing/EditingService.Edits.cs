using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Editing;

public sealed partial class EditingService
{
    public Task<UserSession> SetCropAsync(long userId, string spec, bool album, CancellationToken ct = default) =>
        ApplyEditAsync(userId, edits => edits with { CropSpec = spec, Album = album }, ct);

    public Task<UserSession> RotateAsync(long userId, int quarterTurns, CancellationToken ct = default) =>
        ApplyEditAsync(userId, edits => edits with { Rotate = ((((edits.Rotate + quarterTurns) % 4) + 4) % 4) }, ct);

    public Task<UserSession> SetFilterAsync(long userId, string? filter, CancellationToken ct = default) =>
        ApplyEditAsync(userId, edits => edits with { Filter = NormalizeFilter(filter) }, ct);

    public Task<UserSession> SetPageFormatAsync(long userId, string format, double? widthCm = null, double? heightCm = null, CancellationToken ct = default) =>
        ApplyEditAsync(userId, edits => WithPageFormat(edits, format, widthCm, heightCm), ct);

    // cm dims only ride a custom format.
    private static EditState WithPageFormat(EditState edits, string format, double? widthCm, double? heightCm) =>
        edits with
        {
            PageFormat = format,
            CustomPageWidth = format == "custom" ? widthCm : null,
            CustomPageHeight = format == "custom" ? heightCm : null,
        };

    public Task<UserSession> ApplyLayoutAsync(
        long userId, StencilLayout layout, bool combine = false, CancellationToken ct = default) =>
        ApplyEditAsync(userId, edits =>
        {
            // Combine keeps what is drawn and puts the incoming lines on top, like the editors and
            // the CLI console.
            if (!combine || edits.Layout is null) return edits with { Layout = layout };
            List<LayoutLine> merged = [.. edits.Layout.Lines, .. layout.Lines];
            return edits with { Layout = layout with { Lines = merged } };
        }, ct);

    public Task<UserSession> SetFormulaAsync(long userId, string axis, string expr, CancellationToken ct = default) =>
        ApplyEditAsync(userId, edits =>
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
        return await SaveAsync(EditSessions.With(session, stepped, previous), ct);
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
        return await SaveAsync(EditSessions.With(session, stepped, next), ct);
    }

    public async Task<UserSession> ResetEditsAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        return await SaveAsync(
            EditSessions.With(session, HistoryStack<EditState>.Empty, new EditState()), ct);
    }

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

    private async Task<UserSession> ApplyEditAsync(long userId, Func<UserSession, EditState> mutate, CancellationToken ct)
    {
        var session = await _store.GetAsync(userId, ct);
        return await SaveAsync(EditSessions.WithHistory(session, mutate(session)), ct);
    }

    private Task<UserSession> ApplyEditAsync(long userId, Func<EditState, EditState> mutate, CancellationToken ct) =>
        ApplyEditAsync(userId, session => mutate(session.Edits), ct);

    private async Task<UserSession> SaveAsync(UserSession updated, CancellationToken ct)
    {
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    private static string? NormalizeFilter(string? filter)
    {
        if (string.IsNullOrEmpty(filter) || string.Equals(filter, "none", StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }
        return filter;
    }
}
