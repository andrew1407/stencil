using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Domain.Sessions;

// Persisted as one JSON value; the image bytes live on disk, only the path is carried.
public sealed record UserSession
{
    public required long UserId { get; init; }

    public string? OriginalImagePath { get; init; }

    public int OriginalWidth { get; init; }
    public int OriginalHeight { get; init; }

    public string? ImageLabel { get; init; }

    public string? SourceUrl { get; init; }

    public string? VideoSourcePath { get; init; }

    public EditState Edits { get; init; } = new();

    // Undo/redo snapshots, oldest first; both clear when the working image changes or on reset.
    public IReadOnlyList<EditState> EditHistory { get; init; } = [];

    public IReadOnlyList<EditState> EditRedo { get; init; } = [];

    // Ask-card option labels (§11): callback data is capped at 64 bytes, so the text lives here.
    public IReadOnlyList<string> AskOptions { get; init; } = [];

    public bool AskMulti { get; init; }

    public IReadOnlyList<int> AskPicked { get; init; } = [];

    // Kept for the Retry button (callback data cannot carry the prompt); cleared when a turn lands.
    public string? LastRetryablePrompt { get; init; }

    // Per user: several people share one bot, so one trying a local model must not move the rest.
    public string? LlmProfile { get; init; }

    public IReadOnlyList<ServerConnectionInfo> Connections { get; init; } = [];

    public string? ActiveServerUrl { get; init; }
    public string? ActiveProjectId { get; init; }
    public string? ActiveProjectName { get; init; }

    public string? ActiveProjectDescription { get; init; }

    // Epoch ms; 0 when none.
    public long ActiveProjectCreatedAt { get; init; }

    // Epoch ms; 0 = keep forever.
    public long ActiveProjectExpiresAt { get; init; }

    // The LWW guard for save-back.
    public long ActiveProjectVersion { get; init; }

    // The raw layout JSON as fetched, so a save-back preserves fields the bot doesn't model.
    public string? ActiveProjectLayoutJson { get; init; }

    public bool SyncEnabled { get; init; }

    // Slash commands and PendingInput always win over chat mode.
    public bool ChatMode { get; init; }

    // Contract §12: the store is the active SERVER project's chat file; nothing to write without
    // one.
    public bool SaveChats { get; init; }

    // Chat save-back is best-effort: warn once, re-armed by a successful save.
    public bool ChatSaveWarned { get; init; }

    public string? PendingInput { get; init; }

    public bool HasImage => OriginalImagePath is not null;

    public ServerConnectionInfo? FindConnection(string normalizedUrl) =>
        Connections.FirstOrDefault(c => c.Url == normalizedUrl);
}
