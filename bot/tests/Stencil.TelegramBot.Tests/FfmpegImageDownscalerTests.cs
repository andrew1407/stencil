using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Media;
using Stencil.TelegramBot.Infrastructure.Processes;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The ffmpeg downscaler's argv and failure contract, with the process runner replaced by a
/// recording stub — ffmpeg is never spawned. One shrink-only invocation resizes and re-encodes;
/// every failure returns null so the caller falls back to the original bytes, and the temp
/// output never survives the call.
/// </summary>
public sealed class FfmpegImageDownscalerTests
{
    private readonly BotOptions _options = new() { CliTimeout = TimeSpan.FromSeconds(9) };

    private string[] _argv = [];
    private string _tool = "";
    private TimeSpan _timeout;

    /// <summary>Record the invocation and answer with <paramref name="outcome"/>.</summary>
    private FfmpegImageDownscaler makeDownscaler(ProcessOutcome outcome, bool writeOutput)
    {
        return new FfmpegImageDownscaler(_options, async (file, argv, timeout, ct) =>
        {
            _tool = file;
            _argv = [.. argv];
            _timeout = timeout;
            if (writeOutput)
            {
                await File.WriteAllBytesAsync(argv[^1], new byte[] { 1, 2, 3 }, ct);
            }
            return outcome;
        });
    }

    private static ProcessOutcome Ok => new ProcessCompleted(0, "");

    private string Output => _argv[^1];

    [Fact]
    public async Task Should_Resize_And_Re_Encode_In_One_Shrink_Only_Invocation()
    {
        byte[]? bytes = await makeDownscaler(Ok, writeOutput: true).DownscaleToPngAsync("/tmp/in.jpg", 1024);

        Assert.Equal(new byte[] { 1, 2, 3 }, bytes);
        Assert.Equal("ffmpeg", _tool);
        Assert.Equal(_options.CliTimeout, _timeout); // the CLI's own per-invocation deadline
        Assert.Equal(
            ["-hide_banner", "-loglevel", "error", "-y", "-i", "/tmp/in.jpg", "-frames:v", "1", "-vf",
             "scale=w='min(1024,iw)':h='min(1024,ih)':force_original_aspect_ratio=decrease", Output],
            _argv);
        Assert.EndsWith(".png", Output);                       // the output format is the extension
        Assert.StartsWith(Path.GetTempPath(), Output);         // scratch, not the user's workspace
        Assert.False(File.Exists(Output));                     // …and it is cleaned up
    }

    [Fact]
    public async Task Should_Ride_The_Max_Edge_Into_Both_Scale_Bounds()
    {
        await makeDownscaler(Ok, writeOutput: true).DownscaleToPngAsync("/tmp/in.png", 320);

        Assert.Contains("scale=w='min(320,iw)':h='min(320,ih)':force_original_aspect_ratio=decrease", _argv);
    }

    [Fact]
    public async Task Should_Give_Each_Call_Its_Own_Output_Path()
    {
        FfmpegImageDownscaler downscaler = makeDownscaler(Ok, writeOutput: true);

        await downscaler.DownscaleToPngAsync("/tmp/in.png", 64);
        string first = Output;
        await downscaler.DownscaleToPngAsync("/tmp/in.png", 64);

        Assert.NotEqual(first, Output); // no two concurrent turns share a scratch file
    }

    [Theory]
    [InlineData(1)]    // a format ffmpeg can't read
    [InlineData(255)]
    public async Task Should_Yield_Null_And_Leave_No_Temp_File_On_A_Non_Zero_Exit(int exitCode)
    {
        byte[]? bytes = await makeDownscaler(new ProcessCompleted(exitCode, "boom"), writeOutput: true)
            .DownscaleToPngAsync("/tmp/in.jpg", 1024);

        Assert.Null(bytes);
        Assert.False(File.Exists(Output));
    }

    [Fact]
    public async Task Should_Yield_Null_When_Ffmpeg_Is_Not_Installed()
    {
        Assert.Null(await makeDownscaler(new ProcessStartFailed("no such file"), writeOutput: false)
            .DownscaleToPngAsync("/tmp/in.jpg", 1024));
    }

    [Fact]
    public async Task Should_Yield_Null_On_A_Timeout()
    {
        Assert.Null(await makeDownscaler(new ProcessTimedOut(), writeOutput: false)
            .DownscaleToPngAsync("/tmp/in.jpg", 1024));
    }

    [Fact]
    public async Task Should_Yield_Null_On_A_Clean_Exit_With_No_Output_File()
    {
        // ffmpeg reported success but wrote nothing — treated as a failure, not as empty bytes.
        Assert.Null(await makeDownscaler(Ok, writeOutput: false).DownscaleToPngAsync("/tmp/in.jpg", 1024));
    }

    [Fact]
    public async Task Should_Propagate_A_Caller_Cancel()
    {
        using CancellationTokenSource cts = new();
        FfmpegImageDownscaler downscaler = new(_options, (file, argv, timeout, ct) =>
        {
            _argv = [.. argv];
            throw new OperationCanceledException(cts.Token);
        });

        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            () => downscaler.DownscaleToPngAsync("/tmp/in.jpg", 1024, cts.Token));
        Assert.False(File.Exists(Output)); // the scratch file is still swept
    }
}
