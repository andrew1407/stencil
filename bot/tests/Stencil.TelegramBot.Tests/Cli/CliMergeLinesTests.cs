using System.Text.Json;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Infrastructure.Cli;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests.Cli;

/// <summary><c>--merge-lines</c>: the line sets ride stdin as one object, the merge comes back as one
/// JSON document; with <c>BOT_TEST_CLI</c> set, the live CLI merges every case as the offline double does.</summary>
public sealed class CliMergeLinesTests
{
    private static LayoutLine line(double x, string color = "#FF0000") =>
        new() { Points = [new LayoutPoint(x, 1), new LayoutPoint(x, 9)], Color = color };

    private static readonly LayoutLine _a = line(10), _b = line(20, "#00FF00"), _c = line(30, "#0000FF");

    public static TheoryData<string, LayoutLine[], LayoutLine[], LayoutLine[]> Cases => new()
    {
        { "disjoint", [_a], [_b], [] },
        { "shared line kept once", [_a, _b], [_b, _c], [] },
        { "seen drops a peer's delete", [_b], [_a, _c], [_a] },
        { "local duplicates collapse", [], [_c, _c], [] },
        { "empty", [], [], [] },
    };

    [Fact]
    public void Should_Read_The_Line_Sets_From_Stdin() =>
        Assert.Equal(["--merge-lines", "-"], CliArgvBuilder.BuildMergeLinesArgv());

    [Fact]
    public void Should_Write_Peer_Local_And_Seen_As_One_Object()
    {
        using JsonDocument input = JsonDocument.Parse(CliArgvBuilder.BuildMergeLinesInput([_a], [_b, _c], [_a]));

        Assert.Equal(1, input.RootElement.GetProperty("peer").GetArrayLength());
        Assert.Equal(2, input.RootElement.GetProperty("local").GetArrayLength());
        Assert.Equal("#00FF00", input.RootElement.GetProperty("local")[0].GetProperty("color").GetString());
        Assert.Equal(20, input.RootElement.GetProperty("local")[0].GetProperty("points")[0].GetProperty("x").GetDouble());
        Assert.Equal(1, input.RootElement.GetProperty("seen").GetArrayLength());
    }

    [Fact]
    public void Should_Type_The_Merged_Lines_And_The_Peer_Flag()
    {
        string stdout = $$"""{"version":1,"lines":{{StencilJson.Serialize(new[] { _a, _b })}},"peerAdded":true}""";

        LineMerge merge = CliOutcomeParser.ParseMergeLines(stdout + "\n");

        Assert.Equal(StencilJson.Serialize(new[] { _a, _b }), StencilJson.Serialize(merge.Lines));
        Assert.True(merge.PeerAdded);
    }

    [Theory]
    [InlineData("")]
    [InlineData("not json")]
    [InlineData("[1,2]")]
    [InlineData("""{"version":1}""")]
    [InlineData("""{"version":1,"lines":{}}""")]
    [InlineData("""{"version":1,"lines":[{"points":"nope"}]}""")]
    public void Should_Refuse_Anything_That_Is_Not_A_Line_Merge(string stdout) =>
        Assert.Throws<StencilCliException>(() => CliOutcomeParser.ParseMergeLines(stdout));

    [Fact]
    public void Should_Refuse_Another_Document_Version() =>
        Assert.Contains("version 2", Assert.Throws<StencilCliException>(
            () => CliOutcomeParser.ParseMergeLines("""{"version":2,"lines":[],"peerAdded":false}""")).Message);

    [Theory]
    [MemberData(nameof(Cases))]
    public async Task Should_Merge_As_The_Offline_Double_Does_On_The_Live_Cli(
        string name, LayoutLine[] peer, LayoutLine[] local, LayoutLine[] seen)
    {
        if (PlanCheckRecordings.LiveCli is not string cli)
        {
            return;
        }
        LineMerge live = await PlanCheckRecordings.Live(cli).MergeLinesAsync(peer, local, seen);
        LineMerge offline = await new MockStencilCli().MergeLinesAsync(peer, local, seen);

        Assert.True(StencilJson.Serialize(offline.Lines) == StencilJson.Serialize(live.Lines),
            $"{name}: the live CLI merged {StencilJson.Serialize(live.Lines)}");
    }
}
