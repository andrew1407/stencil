using System.Text.Json;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Project;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Application.Editing;

// The layout is the shared map server projects use too, so a bundle round-trips through any
// surface.
internal sealed class ProjectFileService
{
    private readonly IStencilCli _cli;
    private readonly IEditingService _editing;

    public ProjectFileService(IStencilCli cli, IEditingService editing)
    {
        _cli = cli;
        _editing = editing;
    }

    public async Task<UserSession> OpenAsync(long userId, UserSession session, StencilProject project, CancellationToken ct)
    {
        string extension = string.IsNullOrEmpty(project.ImageExt) ? ".png" : "." + project.ImageExt;
        string path = await _editing.StoreOriginalBytesAsync(userId, project.ImageBytes, extension, ct);
        // The file's own w/h are advisory: read the real ones from the image itself.
        ImageSize size = await ImageDimensionReader.TryReadFileAsync(path, ct) ?? await _cli.ProbeAsync(path, ct);
        string label = string.IsNullOrEmpty(project.Name) ? "project" : project.Name;
        UserSession reset = EditSessions.ResetToImage(session, path, size, label, project.Source);
        EditState edits = project.Layout is JsonElement layout
            ? ProjectLayoutMapper.ToEditState(layout, size.Width, size.Height)
            : new EditState();
        return reset with { Edits = edits, ActiveProjectLayoutJson = project.Layout?.GetRawText() };
    }

    public async Task<byte[]> ExportAsync(long userId, UserSession session, CancellationToken ct)
    {
        if (session.OriginalImagePath is null)
        {
            throw new InvalidOperationException("No working image — upload a photo or use /blank first.");
        }
        byte[] originalBytes = await File.ReadAllBytesAsync(session.OriginalImagePath, ct);
        string ext = Path.GetExtension(session.OriginalImagePath).TrimStart('.');
        if (string.IsNullOrEmpty(ext)) ext = "png";
        // Render for the RESULT dimensions the layout's imageWidth/imageHeight report.
        RenderResult render = await _editing.RenderAsync(userId, ct);
        var layout = ProjectLayoutWriter.Build(session.ActiveProjectLayoutJson, session.Edits, render.Width, render.Height);
        var project = new StencilProject
        {
            Name = string.IsNullOrEmpty(session.ImageLabel) ? "project" : session.ImageLabel!,
            Source = session.SourceUrl,
            ImageBytes = originalBytes,
            ImageExt = ext,
            ImageWidth = session.OriginalWidth,
            ImageHeight = session.OriginalHeight,
            Layout = JsonSerializer.SerializeToElement(layout, StencilJson.Options),
        };
        return StencilProjectFile.BuildUtf8(project);
    }
}
