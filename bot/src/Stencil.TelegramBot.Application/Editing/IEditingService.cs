using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Editing;

/// <summary>
/// The per-user image-editing surface: one base image on disk plus a re-applicable
/// <see cref="EditState"/>. Mutating methods fold an intent into the session's
/// <see cref="EditState"/> and persist it; <see cref="RenderAsync"/> replays the original
/// plus that state through the CLI to a fresh result file without touching the session.
/// </summary>
/// <remarks>
/// Mirrors the CLI console's single working image + ordered transforms
/// (<c>cli/README.md</c> console pipeline: source → crop → rotate → layout → filter), but
/// keeps crop/rotate/filter/layout as the latest re-applicable spec rather than a baked
/// snapshot, so a render is reproducible and the layout JSON stays exportable.
/// </remarks>
public interface IEditingService
{
    /// <summary>
    /// Adopt a local file as the base image: copy it into the user's workspace (keeping its
    /// extension), probe its dimensions, reset the edit state and clear any active project.
    /// <paramref name="sourceUrl"/> records the http(s) origin (e.g. the scraped page) for
    /// display; pass null for a directly-uploaded file.
    /// </summary>
    Task<UserSession> SetImageFromLocalFileAsync(long userId, string sourcePath, string label, string? sourceUrl = null, CancellationToken ct = default);

    /// <summary>
    /// Adopt an http(s) image as the base: download/decode it through the CLI to a fresh PNG
    /// and use the result as the new original (edits reset).
    /// </summary>
    Task<UserSession> SetImageFromUrlAsync(long userId, string url, string label, CancellationToken ct = default);

    /// <summary>
    /// Create a blank canvas through the CLI and adopt it as the base image (label "blank").
    /// </summary>
    Task<UserSession> BlankAsync(long userId, BlankSpec spec, CancellationToken ct = default);

    /// <summary>Set (or replace) the crop spec / album flag on the edit state.</summary>
    Task<UserSession> SetCropAsync(long userId, string spec, bool album, CancellationToken ct = default);

    /// <summary>Accumulate clockwise quarter-turns, normalised to <c>0..3</c>.</summary>
    Task<UserSession> RotateAsync(long userId, int quarterTurns, CancellationToken ct = default);

    /// <summary>Set the filter, or clear it when given null/empty/"none".</summary>
    Task<UserSession> SetFilterAsync(long userId, string? filter, CancellationToken ct = default);

    /// <summary>
    /// Set the session's page format: a canonical ISO name (e.g. <c>B5</c>) or <c>custom</c>
    /// with <paramref name="widthCm"/>/<paramref name="heightCm"/> in cm. A named format is the
    /// <c>/blank</c> default page; either kind rides the saved project layout's <c>pageSize</c>.
    /// </summary>
    Task<UserSession> SetPageFormatAsync(long userId, string format, double? widthCm = null, double? heightCm = null, CancellationToken ct = default);

    /// <summary>Apply a whole drawing layout to the edit state (replaces any current lines).</summary>
    Task<UserSession> ApplyLayoutAsync(long userId, StencilLayout layout, CancellationToken ct = default);

    /// <summary>
    /// Update the pen (the style for newly drawn lines); only non-null arguments change.
    /// <paramref name="style"/> must be <c>solid</c>/<c>dashed</c>/<c>dotted</c>;
    /// <paramref name="fill"/> may be <c>none</c>/<c>transparent</c> to clear a closed-shape fill.
    /// </summary>
    Task<UserSession> ConfigurePenAsync(long userId, string? color, double? thickness, double? markerSize, string? style, string? fill, CancellationToken ct = default);

    /// <summary>
    /// Append a polyline through <paramref name="points"/> (image pixels) styled with the
    /// current pen. When <paramref name="closed"/> the shape is closed (first point repeated)
    /// and filled with the pen's fill colour; an open line is never filled.
    /// </summary>
    Task<UserSession> AddLineAsync(long userId, IReadOnlyList<LayoutPoint> points, bool closed, CancellationToken ct = default);

    /// <summary>Remove the most recently drawn line/shape (no-op when none).</summary>
    Task<UserSession> RemoveLastLineAsync(long userId, CancellationToken ct = default);

    /// <summary>Remove every drawn line/shape, keeping the working image and other edits.</summary>
    Task<UserSession> ClearLinesAsync(long userId, CancellationToken ct = default);

    /// <summary>
    /// Adopt a video as a source: persist it into the workspace, grab frame
    /// <paramref name="frame"/> through the CLI as the working image, and remember the video so
    /// <see cref="ExtractFrameAsync"/> can re-grab a different frame. Needs <c>ffmpeg</c> on PATH.
    /// </summary>
    Task<UserSession> SetImageFromVideoAsync(long userId, string videoSourcePath, int frame, string label, CancellationToken ct = default);

    /// <summary>
    /// Re-grab frame <paramref name="frame"/> from the session's remembered video as the new
    /// working image (resets edits). Throws when no video source is loaded.
    /// </summary>
    Task<UserSession> ExtractFrameAsync(long userId, int frame, CancellationToken ct = default);

    /// <summary>Step back one undoable change (crop/rotate/filter/draw); no-op when none.</summary>
    Task<UserSession> UndoAsync(long userId, CancellationToken ct = default);

    /// <summary>Re-apply the most recently undone change; no-op when there is nothing to redo.</summary>
    Task<UserSession> RedoAsync(long userId, CancellationToken ct = default);

    /// <summary>Clear all pending transforms but keep the working image.</summary>
    Task<UserSession> ResetEditsAsync(long userId, CancellationToken ct = default);

    /// <summary>Drop the working image (and active project) entirely and wipe the workspace.</summary>
    Task<UserSession> DropImageAsync(long userId, CancellationToken ct = default);

    /// <summary>
    /// Persist raw image bytes into the user's workspace and return the on-disk path. Lets
    /// the server service (which owns no workspace) adopt a downloaded server original while
    /// keeping all temp-path handling inside the editing layer.
    /// </summary>
    Task<string> StoreOriginalBytesAsync(long userId, byte[] data, string extension, CancellationToken ct = default);

    /// <summary>
    /// Replay the original plus the current <see cref="EditState"/> through the CLI to a
    /// fresh result file. Does not mutate the session. Throws when no working image is loaded.
    /// </summary>
    Task<RenderResult> RenderAsync(long userId, CancellationToken ct = default);

    /// <summary>
    /// Scrape a web page's media into a fresh per-user scratch directory via the CLI
    /// (<c>--source-site</c> mode), applying the request's category/format/dimension filters and
    /// paging window. Does not touch the working image or session — it just downloads and returns
    /// the matched files. The service fills <see cref="ScrapeRequest.OutputDir"/> with the scratch
    /// path; the caller supplies the URL and filters. Throws when nothing matched or the fetch failed.
    /// </summary>
    Task<ScrapeResult> ScrapeAsync(long userId, ScrapeRequest request, CancellationToken ct = default);

    /// <summary>
    /// Build the exportable <see cref="StencilLayout"/> for a session from its edit state:
    /// the original dimensions, the active filter and the applied layout's lines.
    /// </summary>
    StencilLayout BuildLayout(UserSession session);

    /// <summary>Pretty-print <see cref="BuildLayout"/> as the layout JSON download.</summary>
    string ExportLayoutJson(UserSession session);
}
