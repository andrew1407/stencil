using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests.Cli;

/// <summary><c>ParseWroteProject</c> against the CLI's <c>wrote {path} (project)</c> grammar, the .NET peer of <c>parse_wrote_project</c> in <c>mcp/src/outcome.rs</c>: a project carries no dimensions, so <see cref="CliOutcomeParser.ParseWrote"/> must skip it.</summary>
public sealed class CliOutcomeProjectTests
{
    [Fact]
    public void Should_Parse_The_Project_Path() =>
        Assert.Equal("/tmp/shot.stencil",
            CliOutcomeParser.ParseWroteProject("stencil 1.0\nwrote /tmp/shot.stencil (project)\n"));

    // rfind-free by construction: the suffix is fixed, so a path with " (" survives.
    [Fact]
    public void Should_Parse_A_Path_Containing_Parentheses() =>
        Assert.Equal("/tmp/img (1).stencil",
            CliOutcomeParser.ParseWroteProject("wrote /tmp/img (1).stencil (project)"));

    [Theory]
    [InlineData("\r\n")]
    [InlineData("\r")]
    public void Should_Handle_Every_Newline_Convention(string eol) =>
        Assert.Equal("out.stencil",
            CliOutcomeParser.ParseWroteProject($"banner{eol}wrote out.stencil (project){eol}"));

    [Theory]
    [InlineData("wrote out.png (800x600)")]
    [InlineData("wrote out.png (800x600 px · A4)")]
    [InlineData("error: no such file")]
    [InlineData("")]
    public void Should_Return_Null_When_No_Project_Line_Is_Present(string stderr) =>
        Assert.Null(CliOutcomeParser.ParseWroteProject(stderr));

    // The two parsers partition the `wrote` lines: neither steals the other's.
    [Fact]
    public void Should_Skip_The_Project_Line_In_Parse_Wrote() =>
        Assert.Null(CliOutcomeParser.ParseWrote("wrote /tmp/shot.stencil (project)"));

    [Fact]
    public void Should_Skip_The_Pixel_Line_In_Parse_Wrote_Project() =>
        Assert.Null(CliOutcomeParser.ParseWroteProject("wrote /tmp/out.png (800x600)"));
}
