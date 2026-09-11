using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Editing;

/// <summary>The session folds every edit path shares: the undo/redo stacks and the fresh-image reset.</summary>
internal static class EditSessions
{
    /// <summary>The session's undo/redo stacks as one value.</summary>
    public static HistoryStack<EditState> History(UserSession session) =>
        new(session.EditHistory, session.EditRedo);

    /// <summary>Put a stepped stack and its snapshot back on the session.</summary>
    public static UserSession With(UserSession session, HistoryStack<EditState> history, EditState edits) =>
        session with { Edits = edits, EditHistory = history.Done, EditRedo = history.Undone };

    /// <summary>Push the current state onto the bounded history; a fresh edit clears redo.</summary>
    public static UserSession WithHistory(UserSession session, EditState newEdits) =>
        With(session, History(session).Push(session.Edits), newEdits);

    /// <summary>Reset a session onto a freshly adopted base image, clearing edits and project.</summary>
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
