using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Project;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Editing;

// The per-user editing surface: one base image on disk plus a re-applicable EditState. A
// mutating method folds an intent into that state and persists it; RenderAsync replays the
// original plus the state through the CLI to a fresh file, touching no session.
// Crop/rotate/filter/layout stay the latest re-applicable spec, never a baked snapshot, so a
// render is reproducible and the layout JSON stays exportable.
public interface IEditingService
{
    // Copies the file into the workspace keeping its extension, probes it, resets the edits and
    // clears any active project. sourceUrl records an http(s) origin for display; null for an upload.
    Task<UserSession> SetImageFromLocalFileAsync(long userId, string sourcePath, string label, string? sourceUrl = null, CancellationToken ct = default);

    // Downloads and decodes through the CLI to a fresh PNG; edits reset.
    Task<UserSession> SetImageFromUrlAsync(long userId, string url, string label, CancellationToken ct = default);

    // Label "blank".
    Task<UserSession> BlankAsync(long userId, BlankSpec spec, CancellationToken ct = default);

    Task<UserSession> SetCropAsync(long userId, string spec, bool album, CancellationToken ct = default);

    // Accumulates clockwise, normalised to 0..3.
    Task<UserSession> RotateAsync(long userId, int quarterTurns, CancellationToken ct = default);

    // Null/empty/"none" clears it.
    Task<UserSession> SetFilterAsync(long userId, string? filter, CancellationToken ct = default);

    // A canonical ISO name (B5), or "custom" with widthCm/heightCm. A named format is the /blank
    // default page; either kind rides the saved layout's pageSize.
    Task<UserSession> SetPageFormatAsync(long userId, string format, double? widthCm = null, double? heightCm = null, CancellationToken ct = default);

    // combine appends to the lines already drawn; false (the default) replaces them, mirroring
    // the editors' Combine/Replace prompt.
    Task<UserSession> ApplyLayoutAsync(long userId, StencilLayout layout, bool combine = false, CancellationToken ct = default);

    // Metadata, like the browser's formulaX/Y: never changes the raster, but rides the saved
    // layout so the other front-ends pick it up. An empty expr clears the axis.
    Task<UserSession> SetFormulaAsync(long userId, string axis, string expr, CancellationToken ct = default);

    // Only non-null arguments change. style is solid/dashed/dotted; fill takes none/transparent
    // to clear a closed-shape fill.
    Task<UserSession> ConfigurePenAsync(long userId, string? color, double? thickness, double? pointSize, string? style, string? fill, CancellationToken ct = default);

    // points are image pixels. closed repeats the first point and fills with the pen's fill
    // colour; an open line is never filled.
    Task<UserSession> AddLineAsync(long userId, IReadOnlyList<LayoutPoint> points, bool closed, CancellationToken ct = default);

    Task<UserSession> RemoveLastLineAsync(long userId, CancellationToken ct = default);

    // Keeps the working image and the other edits.
    Task<UserSession> ClearLinesAsync(long userId, CancellationToken ct = default);

    // Persists the video into the workspace and remembers it, so ExtractFrameAsync can re-grab a
    // different frame. Needs ffmpeg on PATH.
    Task<UserSession> SetImageFromVideoAsync(long userId, string videoSourcePath, int frame, string label, CancellationToken ct = default);

    // Resets edits. Throws when no video source is loaded.
    Task<UserSession> ExtractFrameAsync(long userId, int frame, CancellationToken ct = default);

    Task<UserSession> UndoAsync(long userId, CancellationToken ct = default);

    Task<UserSession> RedoAsync(long userId, CancellationToken ct = default);

    // Keeps the working image.
    Task<UserSession> ResetEditsAsync(long userId, CancellationToken ct = default);

    // Drops the working image AND the active project, and wipes the workspace.
    Task<UserSession> DropImageAsync(long userId, CancellationToken ct = default);

    // Lets the server service — which owns no workspace — adopt a downloaded server original
    // while every temp path stays inside the editing layer.
    Task<string> StoreOriginalBytesAsync(long userId, byte[] data, string extension, CancellationToken ct = default);

    // Throws when no working image is loaded.
    Task<RenderResult> RenderAsync(long userId, CancellationToken ct = default);

    // The CLI's contour filter to a fresh PNG: the §7 edge-map attachment.
    Task<RenderResult> RenderContourAsync(long userId, string sourcePath, CancellationToken ct = default);

    // The LLM variant path: each variant renders from a copy of the current state with its own
    // ops folded in, so the session's own state is left alone.
    Task<RenderResult> RenderAsync(long userId, EditState edits, CancellationToken ct = default);

    // Adopts the project's ORIGINAL image and rebuilds the EditState from its layout.
    Task<UserSession> OpenProjectFileAsync(long userId, StencilProject project, CancellationToken ct = default);

    // The ORIGINAL image + export layout + metadata as portable .stencil bytes, openable on
    // every Stencil surface. Throws when there is no image.
    Task<byte[]> ExportProjectFileAsync(long userId, CancellationToken ct = default);

    // --source-site mode into a fresh per-user scratch directory; the service fills
    // request.OutputDir. Touches neither the working image nor the session — it downloads and
    // returns the matches. Throws when nothing matched or the fetch failed.
    Task<ScrapeResult> ScrapeAsync(long userId, ScrapeRequest request, CancellationToken ct = default);

    // The original dimensions, the active filter and the applied layout's lines.
    StencilLayout BuildLayout(UserSession session);

    // BuildLayout, pretty-printed for the JSON download.
    string ExportLayoutJson(UserSession session);
}
