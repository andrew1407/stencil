using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests;

/// <summary>Parameter → argv mapping and validation for <see cref="CliArgvBuilder"/>, a port of <c>mcp/tests/args_test.rs</c> including the collaboration-server flags per <c>cli/CONTRACT.md</c> §1.</summary>
public sealed class CliArgvBuilderTests
{
    [Fact]
    public void Should_Build_Minimal_Input_Output()
    {
        EditRequest req = new()
        {
            Input = "a.png",
            Output = "out.png",
        };
        Assert.Equal(new[] { "-i", "a.png", "--confine-output", "out.png" }, CliArgvBuilder.BuildArgv(req));
    }

    [Fact]
    public void Should_Build_The_Full_Pipeline_Order_And_Flags()
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
    public void Should_Pass_A_Crop_Spec_With_An_Aspect_Token_Through_Verbatim()
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
    public void Should_Pass_A_Negative_Rotate_Through()
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
    public void Should_Build_Blank_With_Dims_Color_And_Album()
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
    public void Should_Build_Blank_With_Default_Size_And_Color_Only()
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
    public void Should_Build_Blank_With_Page_Format_And_Color()
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
    public void Should_Treat_Blank_Page_And_Dimensions_As_Mutually_Exclusive()
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
    public void Should_Emit_The_Frame_Flag_For_Video()
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
    public void Should_Place_Output_As_The_Positional_Last()
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
    public void Should_Treat_Input_And_Blank_As_Mutually_Exclusive()
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
    public void Should_Reject_A_Missing_Source()
    {
        EditRequest req = new()
        {
            Output = "out.png",
        };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(req));
        Assert.Contains("no source", ex.Message);
    }

    [Fact]
    public void Should_Reject_Blank_Half_Dimensions()
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
    public void Should_Reject_An_Empty_Output()
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
