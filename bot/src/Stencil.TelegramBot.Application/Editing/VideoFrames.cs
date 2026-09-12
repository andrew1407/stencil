using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Editing;

namespace Stencil.TelegramBot.Application.Editing;

// The only pixel path that is not a replay of the working image (CLI -i video -f n).
internal sealed class VideoFrames
{
    private readonly IStencilCli _cli;
    private readonly IUserWorkspace _workspace;

    public VideoFrames(IStencilCli cli, IUserWorkspace workspace)
    {
        _cli = cli;
        _workspace = workspace;
    }

    public string Store(long userId, string videoSourcePath)
    {
        string ext = Path.GetExtension(videoSourcePath);
        string stored = _workspace.NewFilePath(userId, ext.Length == 0 ? ".mp4" : ext);
        File.Copy(videoSourcePath, stored, overwrite: true);
        return stored;
    }

    public Task<RenderResult> GrabAsync(long userId, string videoPath, int frame, CancellationToken ct) =>
        _cli.EditAsync(new EditRequest
        {
            Input = videoPath,
            Frame = frame,
            Output = _workspace.NewFilePath(userId, ".png"),
            Overwrite = true,
        }, ct);
}
