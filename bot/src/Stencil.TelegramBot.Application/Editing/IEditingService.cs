using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Project;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Editing;

// One base image on disk plus a re-applicable EditState; RenderAsync replays the original through the
// CLI. Edits stay the latest spec, never a baked snapshot, so a render is reproducible.
public interface IEditingService
{
    // Copies into the workspace, probes, resets edits and clears any active project.
    Task<UserSession> SetImageFromLocalFileAsync(long userId, string sourcePath, string label, string? sourceUrl = null, CancellationToken ct = default);

    Task<UserSession> SetImageFromUrlAsync(long userId, string url, string label, CancellationToken ct = default);

    Task<UserSession> BlankAsync(long userId, BlankSpec spec, CancellationToken ct = default);

    Task<UserSession> SetCropAsync(long userId, string spec, bool album, CancellationToken ct = default);

    // Accumulates clockwise, normalised to 0..3.
    Task<UserSession> RotateAsync(long userId, int quarterTurns, CancellationToken ct = default);

    // Null/empty/"none" clears it.
    Task<UserSession> SetFilterAsync(long userId, string? filter, CancellationToken ct = default);

    // A canonical ISO name (B5) or "custom" with cm; a named format is the /blank default page.
    Task<UserSession> SetPageFormatAsync(long userId, string format, double? widthCm = null, double? heightCm = null, CancellationToken ct = default);

    // combine appends to the lines already drawn; false replaces them (the editors'
    // Combine/Replace).
    Task<UserSession> ApplyLayoutAsync(long userId, StencilLayout layout, bool combine = false, CancellationToken ct = default);

    // Metadata like the browser's formulaX/Y: never changes the raster; an empty expr clears the
    // axis.
    Task<UserSession> SetFormulaAsync(long userId, string axis, string expr, CancellationToken ct = default);

    // Only non-null arguments change; fill takes none/transparent to clear a closed-shape fill.
    Task<UserSession> ConfigurePenAsync(long userId, string? color, double? thickness, double? pointSize, string? style, string? fill, CancellationToken ct = default);

    // points are image pixels; closed repeats the first point and fills with the pen's fill colour.
    Task<UserSession> AddLineAsync(long userId, IReadOnlyList<LayoutPoint> points, bool closed, CancellationToken ct = default);

    Task<UserSession> RemoveLastLineAsync(long userId, CancellationToken ct = default);

    Task<UserSession> ClearLinesAsync(long userId, CancellationToken ct = default);

    // Remembers the video so ExtractFrameAsync can re-grab a frame. Needs ffmpeg on PATH.
    Task<UserSession> SetImageFromVideoAsync(long userId, string videoSourcePath, int frame, string label, CancellationToken ct = default);

    Task<UserSession> ExtractFrameAsync(long userId, int frame, CancellationToken ct = default);

    Task<UserSession> UndoAsync(long userId, CancellationToken ct = default);

    Task<UserSession> RedoAsync(long userId, CancellationToken ct = default);

    // Keeps the working image.
    Task<UserSession> ResetEditsAsync(long userId, CancellationToken ct = default);

    // Drops the working image AND the active project, and wipes the workspace.
    Task<UserSession> DropImageAsync(long userId, CancellationToken ct = default);

    // Lets the server service, which owns no workspace, adopt a downloaded original.
    Task<string> StoreOriginalBytesAsync(long userId, byte[] data, string extension, CancellationToken ct = default);

    Task<RenderResult> RenderAsync(long userId, CancellationToken ct = default);

    // The CLI's contour filter to a fresh PNG: the §7 edge-map attachment.
    Task<RenderResult> RenderContourAsync(long userId, string sourcePath, CancellationToken ct = default);

    // The variant path: renders from a copy of the state, leaving the session's own state alone.
    Task<RenderResult> RenderAsync(long userId, EditState edits, CancellationToken ct = default);

    // Adopts the project's ORIGINAL image and rebuilds the EditState from its layout.
    Task<UserSession> OpenProjectFileAsync(long userId, StencilProject project, CancellationToken ct = default);

    // The ORIGINAL image + export layout + metadata as portable .stencil bytes.
    Task<byte[]> ExportProjectFileAsync(long userId, CancellationToken ct = default);

    // --source-site mode into a fresh per-user scratch directory (the service fills
    // request.OutputDir); touches neither the working image nor the session.
    Task<ScrapeResult> ScrapeAsync(long userId, ScrapeRequest request, CancellationToken ct = default);

    StencilLayout BuildLayout(UserSession session);

    string ExportLayoutJson(UserSession session);
}
