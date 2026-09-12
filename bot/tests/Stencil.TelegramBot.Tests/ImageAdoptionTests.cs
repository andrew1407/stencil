using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Adopting a working image needs its pixel dimensions, and a CLI probe pays a whole process
/// (decode + re-encode to a throwaway PNG) for two integers. A readable header answers it here;
/// anything else still falls back to the probe.
/// </summary>
public sealed class ImageAdoptionTests : IDisposable
{
    private const long _userId = 5;

    private readonly string _root;
    private readonly MockStencilCli _cli = new();
    private readonly InMemorySessionStore _store = new();
    private readonly EditingService _service;

    public ImageAdoptionTests()
    {
        _root = Path.Combine(Path.GetTempPath(), "stencil-bot-adopt-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_root);
        _service = new EditingService(_cli, new UserWorkspace(new BotOptions { DataDir = _root }), _store);
    }

    public void Dispose()
    {
        try { Directory.Delete(_root, recursive: true); } catch { /* best effort */ }
    }

    [Fact]
    public async Task APngHeaderIsReadInsteadOfSpawningAProbe()
    {
        string source = Path.Combine(_root, "in.png");
        await File.WriteAllBytesAsync(source, ImageDimensionReaderTests.Png(1024, 768));

        UserSession session = await _service.SetImageFromLocalFileAsync(_userId, source, "photo");

        Assert.Equal(1024, session.OriginalWidth);
        Assert.Equal(768, session.OriginalHeight);
        Assert.Equal(0, _cli.ProbeCalls);
    }

    [Fact]
    public async Task AHeaderTheReaderCannotParseStillProbes()
    {
        string source = Path.Combine(_root, "in.tif");
        await File.WriteAllBytesAsync(source, "II*\0 not a format the sniffer knows"u8.ToArray());
        _cli.CannedSize = new ImageSize(300, 200);

        UserSession session = await _service.SetImageFromLocalFileAsync(_userId, source, "photo");

        Assert.Equal(300, session.OriginalWidth);
        Assert.Equal(1, _cli.ProbeCalls);
    }
}
