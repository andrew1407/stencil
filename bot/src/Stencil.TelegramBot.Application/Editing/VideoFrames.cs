using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Application.Editing;

/// <summary>
/// Video input: keep the source file in the user's workspace and render one frame of it to a
/// fresh PNG through the CLI (<c>-i video -f n</c>) — the only pixel path that is not a replay
/// of the working image.
/// </summary>
internal sealed class VideoFrames
{
    private readonly IStencilCli _cli;
    private readonly IUserWorkspace _workspace;

    public VideoFrames(IStencilCli cli, IUserWorkspace workspace)
    {
        _cli = cli;
        _workspace = workspace;
    }

    /// <summary>Copy a video into the user's workspace and return its stored path.</summary>
    public string Store(long userId, string videoSourcePath)
    {
        string ext = Path.GetExtension(videoSourcePath);
        string stored = _workspace.NewFilePath(userId, ext.Length == 0 ? ".mp4" : ext);
        File.Copy(videoSourcePath, stored, overwrite: true);
        return stored;
    }

    /// <summary>Render one frame of a stored video to a fresh PNG.</summary>
    public Task<RenderResult> GrabAsync(long userId, string videoPath, int frame, CancellationToken ct) =>
        _cli.EditAsync(new EditRequest
        {
            Input = videoPath,
            Frame = frame,
            Output = _workspace.NewFilePath(userId, ".png"),
            Overwrite = true,
        }, ct);
}
