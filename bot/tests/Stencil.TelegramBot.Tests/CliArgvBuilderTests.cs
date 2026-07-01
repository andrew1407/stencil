using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Parameter → argv mapping and validation guards for <see cref="CliArgvBuilder"/>. A port of
/// <c>mcp/tests/args_test.rs</c> (the bot has no server/remote/surface flags, so those cases
/// are dropped).
/// </summary>
public sealed class CliArgvBuilderTests
{
    [Fact]
    public void MinimalInputOutput()
    {
        EditRequest req = new()
        {
            Input = "a.png",
            Output = "out.png",
        };
        Assert.Equal(new[] { "-i", "a.png", "out.png" }, CliArgvBuilder.BuildArgv(req));
    }

    [Fact]
    public void FullPipelineOrderAndFlags()
    {
        EditRequest req = new()
        {
            Input = "photo.jpg",
            CropSpec = "x1=10% x2=90% y1=10% y2=90%",
            Rotate = 1,
            LayoutPath = "/tmp/layout.json",
            Filter = "sepia",
            Output = "out.png",
        };
        Assert.Equal(
            new[]
            {
                "-i", "photo.jpg",
                "-c", "x1=10% x2=90% y1=10% y2=90%",
                "-r", "1",
                "-l", "/tmp/layout.json",
                "--filter", "sepia",
                "out.png",
            },
            CliArgvBuilder.BuildArgv(req));
    }

    [Fact]
    public void NegativeRotateIsPassedThrough()
    {
        EditRequest req = new()
        {
            Input = "a.png",
            Rotate = -1,
            Output = "out.png",
        };
        IReadOnlyList<string> argv = CliArgvBuilder.BuildArgv(req);
        int i = argv.ToList().IndexOf("-r");
        Assert.Equal("-1", argv[i + 1]);
    }

    [Fact]
    public void BlankWithDimsColorAndAlbum()
    {
        EditRequest req = new()
        {
            Blank = new BlankSpec(800, 600, "red"),
            Album = true,
            Output = "out",
        };
        Assert.Equal(
            new[] { "--blank", "800", "600", "red", "--album", "out" },
            CliArgvBuilder.BuildArgv(req));
    }

    [Fact]
    public void BlankDefaultSizeColorOnly()
    {
        EditRequest req = new()
        {
            Blank = new BlankSpec(Color: "#102030"),
            Output = "page.png",
        };
        Assert.Equal(
            new[] { "--blank", "#102030", "page.png" },
            CliArgvBuilder.BuildArgv(req));
    }

    [Fact]
    public void FrameFlagForVideo()
    {
        EditRequest req = new()
        {
            Input = "clip.mp4",
            Frame = 24,
            Output = "f.png",
        };
        Assert.Equal(
            new[] { "-i", "clip.mp4", "-f", "24", "f.png" },
            CliArgvBuilder.BuildArgv(req));
    }

    [Fact]
    public void OutputIsPositionalLast()
    {
        EditRequest req = new()
        {
            Input = "a.png",
            Filter = "bw",
            Output = "result.png",
        };
        IReadOnlyList<string> argv = CliArgvBuilder.BuildArgv(req);
        Assert.Equal("result.png", argv[^1]);
    }

    [Fact]
    public void InputAndBlankAreMutuallyExclusive()
    {
        EditRequest req = new()
        {
            Input = "a.png",
            Blank = new BlankSpec(),
            Output = "out.png",
        };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(req));
        Assert.Contains("mutually exclusive", ex.Message);
    }

    [Fact]
    public void MissingSourceIsRejected()
    {
        EditRequest req = new()
        {
            Output = "out.png",
        };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(req));
        Assert.Contains("no source", ex.Message);
    }

    [Fact]
    public void BlankHalfDimensionsAreRejected()
    {
        EditRequest req = new()
        {
            Blank = new BlankSpec(Width: 800),
            Output = "out.png",
        };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(req));
        Assert.Contains("together", ex.Message);
    }

    [Fact]
    public void EmptyOutputIsRejected()
    {
        EditRequest req = new()
        {
            Input = "a.png",
            Output = "   ",
        };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(req));
        Assert.Contains("output", ex.Message);
    }
}
