using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests;

/// <summary>Parsing the CLI's stderr into structured results — a port of <c>mcp/tests/outcome_test.rs</c> over <c>cli/CONTRACT.md</c> §2, including the collaboration-server delivery lines.</summary>
public sealed class CliOutcomeParserTests
{
    [Fact]
    public void Should_Parse_The_Success_Line()
    {
        RenderResult? result = CliOutcomeParser.ParseWrote("wrote /tmp/out.png (800x600)\n");
        Assert.NotNull(result);
        Assert.Equal("/tmp/out.png", result!.Path);
        Assert.Equal((800, 600), (result.Width, result.Height));
    }

    [Fact]
    public void Should_Parse_The_Success_Line_Among_Banner_Noise()
    {
        string stderr = "  ___ stencil banner ___\nsome usage text\nwrote out.png (16x12)\n";
        RenderResult? result = CliOutcomeParser.ParseWrote(stderr);
        Assert.NotNull(result);
        Assert.Equal("out.png", result!.Path);
        Assert.Equal((16, 12), (result.Width, result.Height));
    }

    [Fact]
    public void Should_Parse_Rich_Dimensions_With_A_Page_Suffix()
    {
        // The real CLI appends " px · A4 …" after WxH (the cm size uses '×', not 'x').
        RenderResult? result = CliOutcomeParser.ParseWrote("wrote /tmp/out.png (1280x720 px · A4 29.7×21cm)");
        Assert.NotNull(result);
        Assert.Equal("/tmp/out.png", result!.Path);
        Assert.Equal((1280, 720), (result.Width, result.Height));
    }

    [Fact]
    public void Should_Treat_The_Page_Suffix_Label_As_Informational_Whatever_The_Format()
    {
        // The page label reflects the page actually used (e.g. a /blank b5) — still ignored here.
        RenderResult? result = CliOutcomeParser.ParseWrote("wrote /tmp/out.png (665x945 px · B5 17.6×25cm)");
        Assert.NotNull(result);
        Assert.Equal((665, 945), (result!.Width, result.Height));
    }

    [Fact]
    public void Should_Handle_A_Path_Containing_A_Paren()
    {
        RenderResult? result = CliOutcomeParser.ParseWrote("wrote /tmp/my (final) shot.png (1920x1080)");
        Assert.NotNull(result);
        Assert.Equal("/tmp/my (final) shot.png", result!.Path);
        Assert.Equal((1920, 1080), (result.Width, result.Height));
    }

    [Fact]
    public void Should_Return_Null_Without_A_Success_Line()
    {
        Assert.Null(CliOutcomeParser.ParseWrote("error: something went wrong"));
    }

    [Fact]
    public void Should_Return_Null_For_Malformed_Dimensions()
    {
        Assert.Null(CliOutcomeParser.ParseWrote("wrote out.png (widexhigh)"));
        Assert.Null(CliOutcomeParser.ParseWrote("wrote out.png (800-600)"));
    }

    [Fact]
    public void Should_Parse_The_Remote_Update_Line()
    {
        IReadOnlyList<RemoteDelivery> remotes = CliOutcomeParser.ParseRemotes(
            "wrote out.png (800x600)\nupdated server result for project p_x_y (800x600)\n");
        RemoteDelivery.Updated updated = Assert.IsType<RemoteDelivery.Updated>(Assert.Single(remotes));
        Assert.Equal("p_x_y", updated.Id);
        Assert.Equal((800, 600), (updated.Width, updated.Height));
    }

    [Fact]
    public void Should_Parse_The_Remote_Create_Line()
    {
        IReadOnlyList<RemoteDelivery> remotes = CliOutcomeParser.ParseRemotes(
            "wrote out.png (16x12)\ncreated server project \"My Shot\" (p_a_b)\n");
        RemoteDelivery.Created created = Assert.IsType<RemoteDelivery.Created>(Assert.Single(remotes));
        Assert.Equal("My Shot", created.Name);
        Assert.Equal("p_a_b", created.Id);
    }

    [Fact]
    public void Should_Parse_Both_Server_Deliveries_In_One_Run()
    {
        string stderr = "wrote out.png (10x10)\n"
            + "updated server result for project p_1 (10x10)\n"
            + "created server project \"Copy\" (p_2)\n";
        IReadOnlyList<RemoteDelivery> remotes = CliOutcomeParser.ParseRemotes(stderr);
        Assert.Equal(2, remotes.Count);
        Assert.Equal(new RemoteDelivery.Updated("p_1", 10, 10), remotes[0]);
        Assert.Equal(new RemoteDelivery.Created("Copy", "p_2"), remotes[1]);
    }

    [Fact]
    public void Should_Yield_Empty_Without_Server_Lines()
    {
        Assert.Empty(CliOutcomeParser.ParseRemotes("wrote out.png (10x10)"));
    }

    [Fact]
    public void Should_Extract_Error_Lines()
    {
        string stderr = "banner\nusage: stencil ...\nerror: could not parse --crop \"oops\"\n";
        Assert.Equal("error: could not parse --crop \"oops\"", CliOutcomeParser.ExtractErrors(stderr));
    }

    [Fact]
    public void Should_Fall_Back_To_Full_Stderr_Without_An_Error_Prefix()
    {
        Assert.Equal("unexpected text", CliOutcomeParser.ExtractErrors("  unexpected text  "));
    }

    [Fact]
    public void Should_Have_A_Message_For_Empty_Stderr()
    {
        Assert.False(string.IsNullOrEmpty(CliOutcomeParser.ExtractErrors("")));
    }
}
