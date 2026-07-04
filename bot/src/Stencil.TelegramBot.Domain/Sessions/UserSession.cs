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

    /// <summary>Connected servers (insertion order), keyed externally by normalised URL.</summary>
    public IReadOnlyList<ServerConnectionInfo> Connections { get; init; } = [];

    // ── Active fetched server project (the target of /save), if any ──
    public string? ActiveServerUrl { get; init; }
    public string? ActiveProjectId { get; init; }
    public string? ActiveProjectName { get; init; }

    /// <summary>The active project's creation time (epoch ms), shown in /status. 0 when none.</summary>
    public long ActiveProjectCreatedAt { get; init; }

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

    public bool HasImage => OriginalImagePath is not null;

    public ServerConnectionInfo? FindConnection(string normalizedUrl) =>
        Connections.FirstOrDefault(c => c.Url == normalizedUrl);
}
