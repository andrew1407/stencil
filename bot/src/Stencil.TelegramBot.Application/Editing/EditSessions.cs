using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Editing;

internal static class EditSessions
{
    public static HistoryStack<EditState> History(UserSession session) =>
        new(session.EditHistory, session.EditRedo);

    public static UserSession With(UserSession session, HistoryStack<EditState> history, EditState edits) =>
        session with { Edits = edits, EditHistory = history.Done, EditRedo = history.Undone };

    public static UserSession WithHistory(UserSession session, EditState newEdits) =>
        With(session, History(session).Push(session.Edits), newEdits);

    public static UserSession ResetToImage(UserSession session, string path, ImageSize size, string label, string? sourceUrl = null) =>
        session with
        {
            OriginalImagePath = path,
            OriginalWidth = size.Width,
            OriginalHeight = size.Height,
            ImageLabel = label,
            SourceUrl = sourceUrl,
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
}
