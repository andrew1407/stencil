using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Domain.Sessions;

// All per-user state, small enough to persist as one JSON value: the working image BYTES live
// on disk, this only carries the path. Mutated copy-on-write via `with`.
public sealed record UserSession
{
    // The session key.
    public required long UserId { get; init; }

    public string? OriginalImagePath { get; init; }

    public int OriginalWidth { get; init; }
    public int OriginalHeight { get; init; }

    // File name, project name, or "blank".
    public string? ImageLabel { get; init; }

    // The /url link or the scraped page; null for uploaded photos, blanks and server projects.
    public string? SourceUrl { get; init; }

    // Kept so /frame n can re-grab a different frame from the uploaded video.
    public string? VideoSourcePath { get; init; }

    public EditState Edits { get; init; } = new();

    // Undo/redo snapshots (oldest first, bounded). Both are cleared when the working image
    // changes or on reset.
    public IReadOnlyList<EditState> EditHistory { get; init; } = [];

    public IReadOnlyList<EditState> EditRedo { get; init; } = [];

    // The last ask card's option labels (contract §11). Callback data is capped at 64 bytes, far
    // too small to carry labels, so the card's text lives here. Empty = no question outstanding.
    public IReadOnlyList<string> AskOptions { get; init; } = [];

    // True when the card takes several picks (a Send button submits).
    public bool AskMulti { get; init; }

    public IReadOnlyList<int> AskPicked { get; init; } = [];

    // The last assistant turn that failed or was stopped, kept for the Retry button (callback
    // data cannot carry the prompt). A turn that lands clears it, so Retry never re-sends.
    public string? LastRetryablePrompt { get; init; }

    // Per user, not per process: several people share one bot, and one of them trying a local
    // model must not move everyone else. An unconfigured name falls back to the default.
    public string? LlmProfile { get; init; }

    // Insertion order; keyed externally by normalised URL.
    public IReadOnlyList<ServerConnectionInfo> Connections { get; init; } = [];

    // ── Active fetched server project (the target of /save), if any ──
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

    // The raw layout JSON as fetched, kept so a save-back preserves fields the bot doesn't model
    // (cropRect, page format, formulas) while updating the ones it does.
    public string? ActiveProjectLayoutJson { get; init; }

    // Off by default: edits auto-upload and a background poll pulls a peer's change into the chat.
    public bool SyncEnabled { get; init; }

    // Off by default: an unclaimed plain message is handed to the assistant as /prompt would.
    // Slash commands and PendingInput always win, so nothing is ever swallowed.
    public bool ChatMode { get; init; }

    // Contract §12, off by default. The store is the active SERVER project's chat file kind
    // (§12.3 — the bot has no local one), so without an active project this has nothing to write.
    public bool SaveChats { get; init; }

    // Chat save-back is best-effort and must never fail the prompt reply, so the failure warning
    // is surfaced once. A successful save clears this, re-arming it.
    public bool ChatSaveWarned { get; init; }

    // A free-text answer the bot is waiting for (see PendingInputs). Any slash command
    // supersedes and clears it.
    public string? PendingInput { get; init; }

    public bool HasImage => OriginalImagePath is not null;

    public ServerConnectionInfo? FindConnection(string normalizedUrl) =>
        Connections.FirstOrDefault(c => c.Url == normalizedUrl);
}
