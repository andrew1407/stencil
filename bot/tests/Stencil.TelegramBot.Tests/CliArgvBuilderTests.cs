using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Parameter → argv mapping and validation guards for <see cref="CliArgvBuilder"/>. A port of
/// <c>mcp/tests/args_test.rs</c>, including the collaboration-server flags
/// (<c>--server</c>/<c>--remote-update</c>/<c>--remote</c>/<c>--remote-name</c>) per
/// <c>cli/CONTRACT.md</c> §1; only mcp's surface-override cases are out of scope.
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
        Assert.Equal(new[] { "-i", "a.png", "--confine-output", "out.png" }, CliArgvBuilder.BuildArgv(req));
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
                "--confine-output", "out.png",
            },
            CliArgvBuilder.BuildArgv(req));
    }

    [Fact]
    public void CropSpecWithAnAspectTokenRidesThroughVerbatim()
    {
        EditRequest req = new()
        {
            Input = "a.png",
            CropSpec = "x1=10% aspect=4:3",
            Output = "out.png",
        };
        IReadOnlyList<string> argv = CliArgvBuilder.BuildArgv(req);
        int i = argv.ToList().IndexOf("-c");
        Assert.Equal("x1=10% aspect=4:3", argv[i + 1]);
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
            new[] { "--blank", "800", "600", "red", "--album", "--confine-output", "out" },
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
            new[] { "--blank", "#102030", "--confine-output", "page.png" },
            CliArgvBuilder.BuildArgv(req));
    }

    [Fact]
    public void BlankWithPageFormatAndColor()
    {
        EditRequest req = new()
        {
            Blank = new BlankSpec(Color: "pink", Page: "B5"),
            Output = "page.png",
        };
        Assert.Equal(
            new[] { "--blank", "B5", "pink", "--confine-output", "page.png" },
            CliArgvBuilder.BuildArgv(req));
    }

    [Fact]
    public void BlankPageAndDimensionsAreMutuallyExclusive()
    {
        EditRequest req = new()
        {
            Blank = new BlankSpec(800, 600, Page: "A5"),
            Output = "out.png",
        };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(req));
        Assert.Contains("mutually exclusive", ex.Message);
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
            new[] { "-i", "clip.mp4", "-f", "24", "--confine-output", "f.png" },
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
