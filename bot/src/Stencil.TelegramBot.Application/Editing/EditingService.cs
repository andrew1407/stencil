using System.Text.Json;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Project;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Editing;

// One base image on disk plus a re-applicable EditState, replayed through IStencilCli on render.
// InvalidOperationException messages are meant to be surfaced verbatim.
public sealed partial class EditingService : IEditingService
{
    private readonly IStencilCli _cli;
    private readonly IUserWorkspace _workspace;
    private readonly ISessionStore _store;
    private readonly VideoFrames _video;
    private readonly ProjectFileService _projectFiles;

    public EditingService(IStencilCli cli, IUserWorkspace workspace, ISessionStore store)
    {
        _cli = cli;
        _workspace = workspace;
        _store = store;
        _video = new VideoFrames(cli, workspace);
        _projectFiles = new ProjectFileService(cli, this);
    }

    public async Task<UserSession> SetImageFromLocalFileAsync(long userId, string sourcePath, string label, string? sourceUrl = null, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var extension = Path.GetExtension(sourcePath);
        var destination = _workspace.NewFilePath(userId, extension);
        File.Copy(sourcePath, destination, overwrite: true);
        var size = await ImageDimensionReader.TryReadFileAsync(destination, ct) ?? await _cli.ProbeAsync(destination, ct);
        var updated = EditSessions.ResetToImage(session, destination, size, label, sourceUrl);
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    public async Task<UserSession> SetImageFromUrlAsync(long userId, string url, string label, CancellationToken ct = default)
    {
        // Open to any Telegram user: vet the link before the CLI fetches it (schemes, local paths,
        // private hosts).
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
        var updated = EditSessions.ResetToImage(session, result.Path, result.Size, label, url);
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    public async Task<UserSession> BlankAsync(long userId, BlankSpec spec, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        // A stored /format is the default page when the spec names neither; --blank takes only named tokens,
        // so a stored "custom" rides as pixel dims, as the CLI console's defaultBlankSizePx.
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
                spec = spec with { Width = cmToBlankPx(cw), Height = cmToBlankPx(ch) };
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
        var updated = EditSessions.ResetToImage(session, result.Path, result.Size, "blank");
        // The page the blank was made with wins; explicit pixel dims keep the previous /format
        // pick, mirroring the CLI console's doBlank restore order.
        if (spec.Page is string page)
        {
            updated = updated with { Edits = withPageFormat(updated.Edits, page, null, null) };
        }
        else if (customConverted)
        {
            updated = updated with
            {
                Edits = withPageFormat(updated.Edits, "custom", session.Edits.CustomPageWidth, session.Edits.CustomPageHeight),
            };
        }
        else if (session.Edits.PageFormat is string prior
            && (prior != "custom"
                || (session.Edits.CustomPageWidth is > 0 && session.Edits.CustomPageHeight is > 0)))
        {
            updated = updated with
            {
                Edits = withPageFormat(updated.Edits, prior, session.Edits.CustomPageWidth, session.Edits.CustomPageHeight),
            };
        }
        await _store.SaveAsync(updated, ct);
        return updated;
    }

    // cm → px like the core's defaultBlankSizePx: cm / 2.54 * 96.
    private static int cmToBlankPx(double cm)
    {
        var px = (int)(cm / 2.54 * 96.0 + 0.5);
        return px < 1 ? 1 : px;
    }

    public async Task<UserSession> SetImageFromVideoAsync(long userId, string videoSourcePath, int frame, string label, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        var storedVideo = _video.Store(userId, videoSourcePath);
        var result = await _video.GrabAsync(userId, storedVideo, frame, ct);
        return await saveAsync(
            EditSessions.ResetToImage(session, result.Path, result.Size, label) with { VideoSourcePath = storedVideo }, ct);
    }

    public async Task<UserSession> ExtractFrameAsync(long userId, int frame, CancellationToken ct = default)
    {
        var session = await _store.GetAsync(userId, ct);
        if (session.VideoSourcePath is null)
        {
            throw new InvalidOperationException("No video loaded — send a video first, then use /frame n.");
        }
        var video = session.VideoSourcePath;
        var label = session.ImageLabel ?? "frame";
        var result = await _video.GrabAsync(userId, video, frame, ct);
        return await saveAsync(
            EditSessions.ResetToImage(session, result.Path, result.Size, label) with { VideoSourcePath = video }, ct);
    }

    public Task<string> StoreOriginalBytesAsync(long userId, byte[] data, string extension, CancellationToken ct = default) =>
        _workspace.WriteAsync(userId, data, extension, ct);

    public Task<ScrapeResult> ScrapeAsync(long userId, ScrapeRequest request, CancellationToken ct = default)
    {
        // Its own sub-directory, apart from render artifacts; /drop's Clear() wipes the whole user
        // tree.
        string dir = Path.Combine(_workspace.DirectoryFor(userId), "scrape-" + Guid.NewGuid().ToString("N"));
        return _cli.ScrapeAsync(request with { OutputDir = dir }, ct);
    }

    public async Task<UserSession> OpenProjectFileAsync(long userId, StencilProject project, CancellationToken ct = default)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        return await saveAsync(await _projectFiles.OpenAsync(userId, session, project, ct), ct);
    }

    public async Task<byte[]> ExportProjectFileAsync(long userId, CancellationToken ct = default) =>
        await _projectFiles.ExportAsync(userId, await _store.GetAsync(userId, ct), ct);

}
