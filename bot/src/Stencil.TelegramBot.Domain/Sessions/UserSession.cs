using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Domain.Sessions;

/// <summary>
/// All per-user bot state, small enough to persist as one JSON value (in Redis or memory).
/// </summary>
/// <remarks>
/// The working image <i>bytes</i> live on disk under <see cref="OriginalImagePath"/>; this
/// record only carries the path plus the editing intent (<see cref="Edits"/>) and the
/// connected servers / active project. Mutated copy-on-write via <c>with</c> and saved back
/// through <see cref="Abstractions.ISessionStore"/>.
/// </remarks>
public sealed record UserSession
{
    /// <summary>Telegram user id — the session key.</summary>
    public required long UserId { get; init; }

    /// <summary>On-disk path of the base image, or null when no image is loaded.</summary>
    public string? OriginalImagePath { get; init; }

    /// <summary>Dimensions of the base image (0 when none).</summary>
    public int OriginalWidth { get; init; }
    public int OriginalHeight { get; init; }

    /// <summary>A human label for the working image (file name, project name, or "blank").</summary>
    public string? ImageLabel { get; init; }

    /// <summary>
    /// The http(s) URL the working image was loaded from — the link for <c>/url</c>, or the
    /// scraped page for <c>/sourcesite</c>/<c>/sourceupload</c>. Null for uploaded photos, blanks
    /// and fetched server projects. Surfaced in <c>/status</c> and the image caption.
    /// </summary>
    public string? SourceUrl { get; init; }

    /// <summary>
    /// When the working image was extracted from an uploaded video, the on-disk path of that
    /// video — kept so <c>/frame n</c> can re-grab a different frame. Null otherwise.
    /// </summary>
    public string? VideoSourcePath { get; init; }

    /// <summary>The accumulated, re-applicable transforms.</summary>
    public EditState Edits { get; init; } = new();

    /// <summary>
    /// Snapshots of <see cref="Edits"/> taken before each undoable change (oldest first,
    /// bounded). Powers a step-back undo; cleared when the working image changes or on reset.
    /// </summary>
    public IReadOnlyList<EditState> EditHistory { get; init; } = [];

    /// <summary>
    /// Edit states that were undone and can be redone (most recently undone last). A fresh
    /// edit clears this; cleared when the working image changes or on reset.
    /// </summary>
    public IReadOnlyList<EditState> EditRedo { get; init; } = [];

    /// <summary>
    /// The option labels of the last <c>ask</c> card the assistant sent (contract §11), and the
    /// picks made on it so far for a multi-select card. Callback data is capped at 64 bytes, far
    /// too small to carry labels, so the card's text lives here; a new card replaces it and
    /// answering clears it. Empty = no question is outstanding.
    /// </summary>
    public IReadOnlyList<string> AskOptions { get; init; } = [];

    /// <summary>True when the outstanding card takes several picks (a Send button submits).</summary>
    public bool AskMulti { get; init; }

    /// <summary>Indices of <see cref="AskOptions"/> currently ticked on a multi-select card.</summary>
    public IReadOnlyList<int> AskPicked { get; init; } = [];

    /// <summary>
    /// The text of the last assistant turn that did not deliver — it failed, or the user stopped
    /// it — kept so the Retry button on that message can re-run it (callback data is capped at 64
    /// bytes and cannot carry the prompt). A turn that lands clears it, so Retry never re-sends
    /// something already answered.
    /// </summary>
    public string? LastRetryablePrompt { get; init; }

    /// <summary>
    /// The chat API this user picked with <c>/chatapi</c> (a <c>LlmProfile.Name</c>), or null for
    /// the bot's own <c>STENCIL_LLM_*</c> configuration. Per user, not per process: several
    /// people share one bot, and one of them trying a local model must not move everyone else.
    /// A name no longer configured falls back to the default rather than failing the turn.
    /// </summary>
    public string? LlmProfile { get; init; }

    /// <summary>Connected servers (insertion order), keyed externally by normalised URL.</summary>
    public IReadOnlyList<ServerConnectionInfo> Connections { get; init; } = [];

    // ── Active fetched server project (the target of /save), if any ──
    public string? ActiveServerUrl { get; init; }
    public string? ActiveProjectId { get; init; }
    public string? ActiveProjectName { get; init; }

    /// <summary>The active project's free-text description ("" / null = none), shown in /status.</summary>
    public string? ActiveProjectDescription { get; init; }

    /// <summary>The active project's creation time (epoch ms), shown in /status. 0 when none.</summary>
    public long ActiveProjectCreatedAt { get; init; }

    /// <summary>The active project's expiry (epoch ms), shown in /status. 0 = keep forever / none.</summary>
    public long ActiveProjectExpiresAt { get; init; }

    /// <summary>Version last seen for the active project — the LWW guard for save-back.</summary>
    public long ActiveProjectVersion { get; init; }

    /// <summary>
    /// The active project's raw layout JSON as fetched (or last saved). Retained so a save-back
    /// can preserve fields the bot doesn't model (cropRect, page format, formulas) while updating
    /// the ones it does (lines, filter, rotation). Null for a bot-created project.
    /// </summary>
    public string? ActiveProjectLayoutJson { get; init; }

    /// <summary>
    /// Live-sync mode for the active project (off by default). When on, edits auto-upload to the
    /// server (so peers see them) and a background poll auto-pulls a peer's change into the chat —
    /// the bot's take on the CLI's <c>/sync</c>.
    /// </summary>
    public bool SyncEnabled { get; init; }

    /// <summary>
    /// Chat mode (off by default). When on, a plain text message that no other flow claims is
    /// handed to the AI assistant exactly as <c>/prompt &lt;text&gt;</c> would be — so the user can
    /// keep talking without retyping the command. Slash commands and pending free-text prompts
    /// (see <see cref="PendingInput"/>) always win, so nothing is ever swallowed.
    /// </summary>
    public bool ChatMode { get; init; }

    /// <summary>
    /// Chat persistence (contract §12 — off by default). When on, the assistant's conversation
    /// is mirrored to the active <b>server</b> project's <c>chat</c> file kind after each prompt
    /// turn and restored when a project is fetched. The bot has no local chat store (§12.3);
    /// without an active server project the flag simply has nothing to write.
    /// </summary>
    public bool SaveChats { get; init; }

    /// <summary>
    /// True once the user has been warned that the §12 chat save-back is failing (persistence
    /// is best-effort and must never fail the prompt reply — so the warning is surfaced once,
    /// not on every turn). Cleared by the next successful save, re-arming the warning.
    /// </summary>
    public bool ChatSaveWarned { get; init; }

    /// <summary>
    /// A pending free-text prompt this user is expected to answer with their next plain message
    /// (e.g. a custom expiry duration — see <see cref="PendingInputs"/>). Null when the bot isn't
    /// waiting on anything; any slash command supersedes and clears it.
    /// </summary>
    public string? PendingInput { get; init; }

    public bool HasImage => OriginalImagePath is not null;

    public ServerConnectionInfo? FindConnection(string normalizedUrl) =>
        Connections.FirstOrDefault(c => c.Url == normalizedUrl);
}
