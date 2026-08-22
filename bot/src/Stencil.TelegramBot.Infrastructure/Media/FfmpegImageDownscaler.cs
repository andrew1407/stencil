using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Processes;
using Stencil.TelegramBot.Infrastructure.Workspace;

namespace Stencil.TelegramBot.Infrastructure.Media;

/// <summary>
/// <see cref="IImageDownscaler"/> over <c>ffmpeg</c> — the same external tool the video-frame
/// pipeline already depends on. One invocation with a shrink-only <c>scale</c> filter
/// (<c>force_original_aspect_ratio=decrease</c> against a <c>min(max,iw)×min(max,ih)</c> box)
/// resizes and re-encodes to PNG without a separate dimension probe. Every failure — ffmpeg
/// not installed, a format it can't read, a non-zero exit, a timeout — returns null so the
/// caller can fall back to the original bytes; like <c>/frame</c>, ffmpeg is optional.
/// </summary>
public sealed class FfmpegImageDownscaler : IImageDownscaler
{
    private readonly BotOptions _options;

    public FfmpegImageDownscaler(BotOptions options)
    {
        _options = options;
    }

    /// <inheritdoc />
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
            // Reuse the CLI's per-invocation deadline so a hung ffmpeg can't pin the turn. A
            // start failure (ffmpeg not installed) or a timeout degrades to null; a caller
            // cancel propagates like every other async path.
            ProcessOutcome outcome = await ProcessRunner
                .RunAsync("ffmpeg", argv, _options.CliTimeout, ct)
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
