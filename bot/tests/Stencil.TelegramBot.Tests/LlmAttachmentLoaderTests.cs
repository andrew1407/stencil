using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The §7 attachment pipeline decision logic, with the ffmpeg seam mocked: images already
/// ≤ 1568 px on the long edge attach their original bytes without a downscale; oversized (or
/// header-unreadable) ones go through <c>DownscaleToPngAsync</c> and become <c>image/png</c>;
/// a failed downscale (no ffmpeg) degrades to the original bytes; non-image paths yield null.
/// </summary>
public sealed class LlmAttachmentLoaderTests : IDisposable
{
    private readonly string _dir;
    private readonly MockImageDownscaler _downscaler = new();
    private readonly LlmAttachmentLoader _loader;

    public LlmAttachmentLoaderTests()
    {
        _dir = Path.Combine(Path.GetTempPath(), "stencil-bot-attach-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_dir);
        _loader = new LlmAttachmentLoader(_downscaler);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dir, recursive: true); } catch { /* best effort */ }
    }

    private string write(string name, byte[] bytes)
    {
        string path = Path.Combine(_dir, name);
        File.WriteAllBytes(path, bytes);
        return path;
    }

    [Fact]
    public async Task NothingAttachableYieldsNull()
    {
        Assert.Null(await _loader.LoadAsync(null));
        Assert.Null(await _loader.LoadAsync(Path.Combine(_dir, "missing.png")));
        // A format outside the contract's accepted set is never attached.
        Assert.Null(await _loader.LoadAsync(write("image.bmp", [1, 2, 3])));
        Assert.Empty(_downscaler.Calls);
    }

    [Fact]
    public async Task SmallImageAttachesTheOriginalBytesWithoutDownscaling()
    {
        byte[] bytes = ImageDimensionReaderTests.Png(1568, 480);
        string path = write("small.png", bytes);

        LlmImage? image = await _loader.LoadAsync(path);

        Assert.Equal("image/png", image!.MediaType);
        Assert.Equal(Convert.ToBase64String(bytes), image.Base64Data);
        Assert.Empty(_downscaler.Calls); // exactly at the bound — no ffmpeg run
    }

    [Fact]
    public async Task SmallWebpKeepsItsOwnMediaType()
    {
        byte[] bytes = ImageDimensionReaderTests.WebpLossy(800, 600);
        LlmImage? image = await _loader.LoadAsync(write("small.webp", bytes));

        Assert.Equal("image/webp", image!.MediaType);
        Assert.Empty(_downscaler.Calls);
    }

    [Fact]
    public async Task OversizedImageIsDownscaledAndBecomesPng()
    {
        _downscaler.Result = [9, 9, 9];
        string path = write("big.jpg", ImageDimensionReaderTests.Jpeg(4000, 500));

        LlmImage? image = await _loader.LoadAsync(path);

        (string calledPath, int maxLongEdge) = Assert.Single(_downscaler.Calls);
        Assert.Equal(path, calledPath);
        Assert.Equal(LlmImage.MaxLongEdgePixels, maxLongEdge); // the contract's 1568
        Assert.Equal(1568, maxLongEdge);
        Assert.Equal("image/png", image!.MediaType); // re-encoded, whatever the source was
        Assert.Equal(Convert.ToBase64String(new byte[] { 9, 9, 9 }), image.Base64Data);
    }

    [Fact]
    public async Task FailedDownscaleFallsBackToTheOriginalBytes()
    {
        _downscaler.Result = null; // ffmpeg unavailable / failed
        byte[] bytes = ImageDimensionReaderTests.Jpeg(4000, 3000);
        string path = write("big.jpg", bytes);

        LlmImage? image = await _loader.LoadAsync(path);

        Assert.Single(_downscaler.Calls);
        Assert.Equal("image/jpeg", image!.MediaType); // original type, original bytes
        Assert.Equal(Convert.ToBase64String(bytes), image.Base64Data);
    }

    [Fact]
    public async Task UnchangedFileIsMemoizedAcrossLoads()
    {
        _downscaler.Result = [9, 9, 9];
        string path = write("big.jpg", ImageDimensionReaderTests.Jpeg(4000, 500));

        LlmImage? first = await _loader.LoadAsync(path);
        LlmImage? second = await _loader.LoadAsync(path);

        // Chat mode re-attaches the working image every turn — one ffmpeg run, not two.
        Assert.Single(_downscaler.Calls);
        Assert.Same(first, second);
    }

    [Fact]
    public async Task ARewrittenFileInvalidatesTheMemoizedAttachment()
    {
        _downscaler.Result = [9, 9, 9];
        string path = write("big.jpg", ImageDimensionReaderTests.Jpeg(4000, 500));
        await _loader.LoadAsync(path);

        // New content (different length ⇒ different key) — the attachment is rebuilt.
        File.WriteAllBytes(path, [.. ImageDimensionReaderTests.Jpeg(4000, 500), 0]);
        await _loader.LoadAsync(path);

        Assert.Equal(2, _downscaler.Calls.Count);
    }

    // The fallback used to send the original at any size, bounded only by the 50 MB download
    // cap. Past the cli/mcp threshold the turn goes text-only instead.
    [Fact]
    public async Task AnOversizedImageThatCannotBeScaledIsSkippedNotSentWhole()
    {
        _downscaler.Result = null;   // no ffmpeg
        byte[] header = ImageDimensionReaderTests.Jpeg(4000, 3000);
        byte[] bytes = new byte[9 * 1024 * 1024];
        header.CopyTo(bytes, 0);

        Assert.Null(await _loader.LoadAsync(write("huge.jpg", bytes)));
        Assert.Single(_downscaler.Calls);   // the scaler was tried first
    }

    [Fact]
    public async Task AnOversizedImageThatScalesDownIsStillAttached()
    {
        _downscaler.Result = [1, 2, 3];
        byte[] header = ImageDimensionReaderTests.Jpeg(4000, 3000);
        byte[] bytes = new byte[9 * 1024 * 1024];
        header.CopyTo(bytes, 0);

        LlmImage? image = await _loader.LoadAsync(write("huge-ok.jpg", bytes));
        Assert.Equal("image/png", image!.MediaType);
        Assert.Equal(Convert.ToBase64String([1, 2, 3]), image.Base64Data);
    }

    [Fact]
    public async Task UnreadableHeaderTriesTheShrinkOnlyDownscale()
    {
        // Dimensions unknown — a shrink-only pass is attempted; here it "fails" (no ffmpeg),
        // so the original bytes attach unchanged.
        byte[] bytes = [0x00, 0x01, 0x02, 0x03];
        LlmImage? image = await _loader.LoadAsync(write("odd.png", bytes));

        Assert.Single(_downscaler.Calls);
        Assert.Equal("image/png", image!.MediaType);
        Assert.Equal(Convert.ToBase64String(bytes), image.Base64Data);
    }
}
