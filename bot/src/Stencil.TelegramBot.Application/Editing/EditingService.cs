using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Editing;

/// <summary>
/// Default <see cref="IEditingService"/>: one base image on disk plus a re-applicable
/// <see cref="EditState"/>, replayed through <see cref="IStencilCli"/> on render.
/// </summary>
/// <remarks>
/// Mutating-edit methods load the session, fold an intent into <see cref="EditState"/> and
/// save it back; <see cref="RenderAsync"/> reads the session and maps the original plus the
/// edit state to one <see cref="EditRequest"/> (the CLI parses flags order-independently, so
/// the fixed pipeline order lives in the CLI itself, per <c>cli/README.md</c>). Friendly
/// <see cref="InvalidOperationException"/> messages are meant to be surfaced verbatim.
/// </remarks>
public sealed class EditingService : IEditingService
{
    private readonly IStencilCli _cli;
    private readonly IUserWorkspace _workspace;
    private readonly ISessionStore _store;

    public EditingService(IStencilCli cli, IUserWorkspace workspace, ISessionStore store)
    {
        _cli = cli;
        _workspace = workspace;
        _store = store;
    }

    /// <inheritdoc />
    public async Task<UserSession> SetImageFromLocalFileAsync(long userId, string sourcePath, string label, string? sourceUrl = null, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var extension = Path.GetExtension(sourcePath);
        var destination = _workspace.NewFilePath(userId, extension);
        File.Copy(sourcePath, destination, overwrite: true);
        var size = await _cli.ProbeAsync(destination, ct);
        var updated = ResetToImage(session, destination, size, label, sourceUrl);
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <inheritdoc />
    public async Task<UserSession> SetImageFromUrlAsync(long userId, string url, string label, CancellationToken ct = default)
    {
        // The bot is open to any Telegram user, so vet the link before the CLI fetches it:
        // reject non-http(s) schemes, bare local paths, and private/loopback/metadata hosts.
        await RemoteImageUrl.ValidateAsync(url, ct);
        var session = await _store.GetAsync(userId, ct);
        var output = _workspace.NewFilePath(userId, ".png");
        var request = new EditRequest
        {
            Input = url,
            Output = output,
            Overwrite = true,
        };
        var result = await _cli.EditAsync(request, ct);
        var updated = ResetToImage(session, result.Path, result.Size, label, url);
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <inheritdoc />
    public async Task<UserSession> BlankAsync(long userId, BlankSpec spec, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        // A stored /format becomes the default page when the spec names neither a page nor
        // explicit dims. The CLI's --blank only takes named format tokens, so a stored
        // "custom" rides as explicit pixel dims instead, converted from the stored cm the
        // same way the CLI console does (core defaultBlankSizePx: cm / 2.54 * 96 dpi,
        // rounded, never below 1 px) — the raster must match the layout's declared page.
        var customConverted = false;
        if (spec.Page is null && spec.Width is null && spec.Height is null
            && session.Edits.PageFormat is string stored)
        {
            if (stored != "custom")
            {
                spec = spec with { Page = stored };
            }
            else if (session.Edits.CustomPageWidth is double cw && cw > 0
                && session.Edits.CustomPageHeight is double ch && ch > 0)
            {
                spec = spec with { Width = CmToBlankPx(cw), Height = CmToBlankPx(ch) };
                customConverted = true;
            }
        }
        var output = _workspace.NewFilePath(userId, ".png");
        var request = new EditRequest
        {
            Blank = spec,
            Output = output,
            Overwrite = true,
        };
        var result = await _cli.EditAsync(request, ct);
        var updated = ResetToImage(session, result.Path, result.Size, "blank");
        // Carry a page format onto the fresh canvas so a later /save writes the layout's
        // pageSize: the page the blank was made with (an explicit token, the injected stored
        // format, or the converted custom cm dims) wins; a blank made from explicit pixel
        // dims keeps the previous /format pick instead, mirroring the CLI console's doBlank
        // restore order (a stored "custom" is only restorable when both cm dims are set).
        if (spec.Page is string page)
        {
            updated = updated with { Edits = WithPageFormat(updated.Edits, page, null, null) };
        }
        else if (customConverted)
        {
            updated = updated with
            {
                Edits = WithPageFormat(updated.Edits, "custom", session.Edits.CustomPageWidth, session.Edits.CustomPageHeight),
            };
        }
        else if (session.Edits.PageFormat is string prior
            && (prior != "custom"
                || (session.Edits.CustomPageWidth is > 0 && session.Edits.CustomPageHeight is > 0)))
        {
            updated = updated with
            {
                Edits = WithPageFormat(updated.Edits, prior, session.Edits.CustomPageWidth, session.Edits.CustomPageHeight),
            };
        }
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <summary>
    /// Convert a page dimension in cm to blank-canvas pixels exactly like the core's
    /// <c>defaultBlankSizePx</c> (mirrored by the CLI console and pystencil REPL):
    /// <c>cm / 2.54 * 96</c>, rounded half-up, never below 1 px.
    /// </summary>
    private static int CmToBlankPx(double cm)
    {
        var px = (int)(cm / 2.54 * 96.0 + 0.5);
        return px < 1 ? 1 : px;
    }

    /// <inheritdoc />
    public Task<UserSession> SetCropAsync(long userId, string spec, bool album, CancellationToken ct = default) =>
        ApplyEditAsync(userId, edits => edits with { CropSpec = spec, Album = album }, ct);

    /// <inheritdoc />
    public Task<UserSession> RotateAsync(long userId, int quarterTurns, CancellationToken ct = default) =>
        ApplyEditAsync(userId, edits => edits with { Rotate = ((((edits.Rotate + quarterTurns) % 4) + 4) % 4) }, ct);

    /// <inheritdoc />
    public Task<UserSession> SetFilterAsync(long userId, string? filter, CancellationToken ct = default) =>
        ApplyEditAsync(userId, edits => edits with { Filter = NormalizeFilter(filter) }, ct);

    /// <inheritdoc />
    public Task<UserSession> SetPageFormatAsync(long userId, string format, double? widthCm = null, double? heightCm = null, CancellationToken ct = default) =>
        ApplyEditAsync(userId, edits => WithPageFormat(edits, format, widthCm, heightCm), ct);

    /// <summary>Set the page format on an edit state; cm dims only ride a <c>custom</c> format.</summary>
    private static EditState WithPageFormat(EditState edits, string format, double? widthCm, double? heightCm) =>
        edits with
        {
            PageFormat = format,
            CustomPageWidth = format == "custom" ? widthCm : null,
            CustomPageHeight = format == "custom" ? heightCm : null,
        };

    /// <inheritdoc />
    public Task<UserSession> ApplyLayoutAsync(long userId, StencilLayout layout, CancellationToken ct = default) =>
        ApplyEditAsync(userId, edits => edits with { Layout = layout }, ct);

    /// <inheritdoc />
    public async Task<UserSession> UndoAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.EditHistory.Count == 0)
        {
            return session;
        }
        var previous = session.EditHistory[^1];
        var history = session.EditHistory.Take(session.EditHistory.Count - 1).ToList();
        var redo = Bounded(session.EditRedo.Append(session.Edits));
        var updated = session with { Edits = previous, EditHistory = history, EditRedo = redo };
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <inheritdoc />
    public async Task<UserSession> RedoAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.EditRedo.Count == 0)
        {
            return session;
        }
        var next = session.EditRedo[^1];
        var redo = session.EditRedo.Take(session.EditRedo.Count - 1).ToList();
        var history = Bounded(session.EditHistory.Append(session.Edits));
        var updated = session with { Edits = next, EditHistory = history, EditRedo = redo };
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <inheritdoc />
    public async Task<UserSession> ResetEditsAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var updated = session with { Edits = new EditState(), EditHistory = [], EditRedo = [] };
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <inheritdoc />
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
            ActiveProjectCreatedAt = 0,
            ActiveProjectVersion = 0,
            ActiveProjectLayoutJson = null,
        };
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <summary>Number of edit snapshots kept for undo (older ones are dropped).</summary>
    private const int MaxHistory = 25;

    /// <summary>Load, fold a new edit state, record the prior state for undo, and persist.</summary>
    private async Task<UserSession> ApplyEditAsync(long userId, Func<UserSession, EditState> mutate, CancellationToken ct)
    {
        var session = await _store.GetAsync(userId, ct);
        var updated = WithHistory(session, mutate(session));
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <summary>Convenience overload whose mutator only needs the current <see cref="EditState"/>.</summary>
    private Task<UserSession> ApplyEditAsync(long userId, Func<EditState, EditState> mutate, CancellationToken ct) =>
        ApplyEditAsync(userId, session => mutate(session.Edits), ct);

    /// <summary>
    /// Push the session's current edit state onto the bounded history and set the new one. A
    /// fresh edit clears the redo stack (you can't redo past a new branch).
    /// </summary>
    private static UserSession WithHistory(UserSession session, EditState newEdits)
    {
        var history = Bounded(session.EditHistory.Append(session.Edits));
        return session with { Edits = newEdits, EditHistory = history, EditRedo = [] };
    }

    /// <summary>Keep only the most recent <see cref="MaxHistory"/> entries of an undo/redo stack.</summary>
    private static List<EditState> Bounded(IEnumerable<EditState> stack)
    {
        var list = stack.ToList();
        if (list.Count > MaxHistory)
        {
            list = list.Skip(list.Count - MaxHistory).ToList();
        }
        return list;
    }

    /// <inheritdoc />
    public async Task<UserSession> ConfigurePenAsync(long userId, string? color, double? thickness, double? markerSize, string? style, string? fill, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var pen = session.Edits.Pen;
        var updatedPen = pen with
        {
            Color = color ?? pen.Color,
            Thickness = thickness ?? pen.Thickness,
            MarkerSize = markerSize ?? pen.MarkerSize,
            Style = style ?? pen.Style,
            FillColor = NormalizeFill(fill) ?? pen.FillColor,
        };
        var updated = session with { Edits = session.Edits with { Pen = updatedPen } };
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <inheritdoc />
    public Task<UserSession> AddLineAsync(long userId, IReadOnlyList<LayoutPoint> points, bool closed, CancellationToken ct = default) =>
        ApplyEditAsync(userId, session =>
        {
            var pen = session.Edits.Pen;
            var pts = points.ToList();
            if (closed && pts.Count >= 1)
            {
                var first = pts[0];
                var last = pts[^1];
                if (last.X != first.X || last.Y != first.Y)
                {
                    pts.Add(first);
                }
            }
            var line = new LayoutLine
            {
                Points = pts,
                Color = pen.Color,
                Thickness = pen.Thickness,
                MarkerSize = pen.MarkerSize,
                Style = pen.Style,
                Locked = closed,
                FillColor = closed ? pen.FillColor : LayoutLine.DefaultFillColor,
            };
            var layout = session.Edits.Layout ?? EmptyLayout(session);
            var lines = layout.Lines.Append(line).ToList();
            var updatedLayout = layout with
            {
                Lines = lines,
                ImageWidth = session.OriginalWidth,
                ImageHeight = session.OriginalHeight,
            };
            return session.Edits with { Layout = updatedLayout };
        }, ct);

    /// <inheritdoc />
    public async Task<UserSession> RemoveLastLineAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var layout = session.Edits.Layout;
        if (layout is null || layout.Lines.Count == 0)
        {
            return session;
        }
        var lines = layout.Lines.Take(layout.Lines.Count - 1).ToList();
        var updatedLayout = lines.Count == 0 ? null : layout with { Lines = lines };
        var updated = WithHistory(session, session.Edits with { Layout = updatedLayout });
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <inheritdoc />
    public async Task<UserSession> ClearLinesAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.Edits.Layout is null)
        {
            return session;
        }
        var updated = WithHistory(session, session.Edits with { Layout = null });
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <inheritdoc />
    public async Task<UserSession> SetImageFromVideoAsync(long userId, string videoSourcePath, int frame, string label, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var ext = Path.GetExtension(videoSourcePath);
        var storedVideo = _workspace.NewFilePath(userId, ext.Length == 0 ? ".mp4" : ext);
        File.Copy(videoSourcePath, storedVideo, overwrite: true);
        var result = await GrabFrameAsync(userId, storedVideo, frame, ct);
        var updated = ResetToImage(session, result.Path, result.Size, label) with { VideoSourcePath = storedVideo };
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <inheritdoc />
    public async Task<UserSession> ExtractFrameAsync(long userId, int frame, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.VideoSourcePath is null)
        {
            throw new InvalidOperationException("No video loaded — send a video first, then use /frame n.");
        }
        var video = session.VideoSourcePath;
        var label = session.ImageLabel ?? "frame";
        var result = await GrabFrameAsync(userId, video, frame, ct);
        var updated = ResetToImage(session, result.Path, result.Size, label) with { VideoSourcePath = video };
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    /// <summary>Render one video frame to a fresh PNG via the CLI (<c>-i video -f n</c>).</summary>
    private Task<RenderResult> GrabFrameAsync(long userId, string videoPath, int frame, CancellationToken ct)
    {
        var request = new EditRequest
        {
            Input = videoPath,
            Frame = frame,
            Output = _workspace.NewFilePath(userId, ".png"),
            Overwrite = true,
        };
        return _cli.EditAsync(request, ct);
    }

    /// <inheritdoc />
    public Task<string> StoreOriginalBytesAsync(long userId, byte[] data, string extension, CancellationToken ct = default) =>
        _workspace.WriteAsync(userId, data, extension, ct);

    /// <inheritdoc />
    public async Task<RenderResult> RenderAsync(long userId, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.OriginalImagePath is null)
        {
            throw new InvalidOperationException("No working image — upload a photo or use /blank first.");
        }
        var edits = session.Edits;
        string? layoutPath = null;
        if (edits.Layout is not null)
        {
            var json = StencilJson.Serialize(edits.Layout);
            var bytes = System.Text.Encoding.UTF8.GetBytes(json);
            layoutPath = await _workspace.WriteAsync(userId, bytes, ".json", ct);
        }
        var request = new EditRequest
        {
            Input = session.OriginalImagePath,
            CropSpec = edits.CropSpec,
            Album = edits.Album,
            Rotate = edits.Rotate == 0 ? null : edits.Rotate,
            Filter = edits.Filter,
            LayoutPath = layoutPath,
            Output = _workspace.NewFilePath(userId, ".png"),
            Overwrite = true,
        };
        return await _cli.EditAsync(request, ct);
    }

    /// <inheritdoc />
    public Task<ScrapeResult> ScrapeAsync(long userId, ScrapeRequest request, CancellationToken ct = default)
    {
        // The scrape writes a directory of downloads, so give it its own fresh sub-directory in
        // the user's workspace (kept apart from the render/layout artifacts). The CLI creates the
        // directory itself; /drop's Clear() wipes the whole user tree, subdir included.
        string dir = Path.Combine(_workspace.DirectoryFor(userId), "scrape-" + Guid.NewGuid().ToString("N"));
        return _cli.ScrapeAsync(request with { OutputDir = dir }, ct);
    }

    /// <inheritdoc />
    public StencilLayout BuildLayout(UserSession session) =>
        new()
        {
            ImageWidth = session.OriginalWidth,
            ImageHeight = session.OriginalHeight,
            Filter = session.Edits.Filter,
            Lines = session.Edits.Layout?.Lines ?? [],
        };

    /// <inheritdoc />
    public string ExportLayoutJson(UserSession session) =>
        StencilJson.SerializeIndented(BuildLayout(session));

    /// <summary>
    /// Reset a session onto a freshly adopted base image: store the path/dimensions/label,
    /// clear the edit state and any active server project.
    /// </summary>
    private static UserSession ResetToImage(UserSession session, string path, ImageSize size, string label, string? sourceUrl = null) =>
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
            ActiveProjectCreatedAt = 0,
            ActiveProjectVersion = 0,
            ActiveProjectLayoutJson = null,
        };

    /// <summary>A fresh empty layout carrying the working image's dimensions.</summary>
    private static StencilLayout EmptyLayout(UserSession session) =>
        new()
        {
            ImageWidth = session.OriginalWidth,
            ImageHeight = session.OriginalHeight,
            Lines = [],
        };

    /// <summary>
    /// Normalise a pen fill argument: null keeps the current fill; <c>none</c>/<c>clear</c>/
    /// <c>transparent</c> (or blank) clears it to <c>transparent</c>; otherwise the colour as-is.
    /// </summary>
    private static string? NormalizeFill(string? fill)
    {
        if (fill is null)
        {
            return null;
        }
        if (string.IsNullOrWhiteSpace(fill) || fill is "none" or "clear" or "transparent")
        {
            return LayoutLine.DefaultFillColor;
        }
        return fill;
    }

    /// <summary>Map null/empty/"none" to a cleared filter; otherwise keep the spec.</summary>
    private static string? NormalizeFilter(string? filter)
    {
        if (string.IsNullOrEmpty(filter) || string.Equals(filter, "none", StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }
        return filter;
    }
}
