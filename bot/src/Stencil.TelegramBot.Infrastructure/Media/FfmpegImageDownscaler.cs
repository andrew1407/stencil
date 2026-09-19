using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Processes;
using Stencil.TelegramBot.Infrastructure.Workspace;

namespace Stencil.TelegramBot.Infrastructure.Media;

// One ffmpeg call with a shrink-only scale filter (force_original_aspect_ratio=decrease), re-encoded to
// PNG; every failure returns null so the caller keeps the original — like /frame, ffmpeg is optional.
public sealed class FfmpegImageDownscaler : IImageDownscaler
{
    private readonly BotOptions _options;
    private readonly Func<string, IReadOnlyList<string>, TimeSpan, CancellationToken, Task<ProcessOutcome>> _run;

    public FfmpegImageDownscaler(
        BotOptions options,
        Func<string, IReadOnlyList<string>, TimeSpan, CancellationToken, Task<ProcessOutcome>>? run = null)
    {
        _options = options;
        _run = run ?? ((file, argv, timeout, ct) => ProcessRunner.RunAsync(file, argv, timeout, ct));
    }

    public async Task<byte[]?> DownscaleToPngAsync(string path, int maxLongEdge, CancellationToken ct = default)
    {
        string output = Path.Combine(Path.GetTempPath(), $"stencil-llm-scale-{Guid.NewGuid():N}.png");
        try
        {
            string[] argv =
            [
                "-hide_banner", "-loglevel", "error", "-y",
                "-i", path,
                "-frames:v", "1",
                "-vf", $"scale=w='min({maxLongEdge},iw)':h='min({maxLongEdge},ih)':force_original_aspect_ratio=decrease",
                output,
            ];
            // The CLI's per-invocation deadline so a hung ffmpeg can't pin the turn; a caller
            // cancel propagates.
            ProcessOutcome outcome = await _run("ffmpeg", argv, _options.CliTimeout, ct)
                .ConfigureAwait(false);
            if (outcome is not ProcessCompleted { ExitCode: 0 } || !File.Exists(output))
            {
                return null;
            }
            return await File.ReadAllBytesAsync(output, ct).ConfigureAwait(false);
        }
        finally
        {
            TempFiles.TryDelete(output);
        }
    }
}
