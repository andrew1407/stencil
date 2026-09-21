using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests.Cli;

/// <summary>The <c>--script-plan</c> envelope (cli/CONTRACT.md §5): one JSON object on stdout, diagnostics always, blocks only when the script is clean. Its <c>script</c> label is this adapter's temp leaf, so nothing user-facing may carry it.</summary>
public sealed class CliOutcomeScriptTests
{
    private const string _envelope = """
    {"version":1,"script":"script-9f3c.stc","diagnostics":[],
     "blocks":[{"index":0,"source":"https://e.example/a.png","sourceKind":"url",
                "inputs":["https://e.example/a.png"],"frame":0,"dims":null,
                "plans":[{"reply":"","actions":[{"op":"openUrl","url":"https://e.example/a.png"},
                                                {"op":"filter","mode":"bw"}]}],
                "saves":[{"input":"https://e.example/a.png","path":"a-stencil.png"}]}]}
    """;

    private const string _failed = """
    {"version":1,"script":"script-9f3c.stc",
     "diagnostics":[{"severity":"error","code":"E_UNKNOWN_DIRECTIVE","line":2,"col":3,"len":4,
                     "message":"unknown directive '@crp'"},
                    {"severity":"warning","code":"W_NO_SAVE","line":1,"col":1,"len":7,
                     "message":"the block never saves"}],
     "blocks":[]}
    """;

    [Fact]
    public void Should_Read_One_Block_And_Its_Plans()
    {
        ScriptPlan plan = CliOutcomeParser.ParseScriptPlan(_envelope);

        ScriptBlock block = Assert.Single(plan.Blocks);
        Assert.Equal(0, block.Index);
        Assert.Equal("url", block.SourceKind);
        Assert.Equal("https://e.example/a.png", block.Source);
        Assert.Contains("\"op\":\"filter\"", Assert.Single(block.Plans).Replace(" ", ""));
        Assert.False(plan.HasErrors);
    }

    /// <summary>A url or project block may run; file/dir/glob names a disk the bot has not got.</summary>
    [Theory]
    [InlineData("url", false)]
    [InlineData("project", false)]
    [InlineData("file", true)]
    [InlineData("dir", true)]
    [InlineData("glob", true)]
    public void Should_Mark_Only_Disk_Sources_As_Local(string kind, bool local)
    {
        ScriptPlan plan = CliOutcomeParser.ParseScriptPlan(
            $$"""{"version":1,"script":"s.stc","diagnostics":[],"blocks":[{"index":0,"source":"x","sourceKind":"{{kind}}","inputs":[],"plans":[]}]}""");

        Assert.Equal(local, plan.Blocks[0].IsLocalSource);
    }

    [Fact]
    public void Should_Keep_The_Diagnostics_Of_A_Script_That_Lowered_To_Nothing()
    {
        ScriptPlan plan = CliOutcomeParser.ParseScriptPlan(_failed);

        Assert.True(plan.HasErrors);
        Assert.Empty(plan.Blocks);
        ScriptDiagnostic error = Assert.Single(plan.Errors);
        Assert.Equal(("E_UNKNOWN_DIRECTIVE", 2, 3), (error.Code, error.Line, error.Col));
        Assert.Equal("W_NO_SAVE", Assert.Single(plan.Warnings).Code);
    }

    /// <summary>The temp leaf never survives the parse, so no reply can echo a workspace path.</summary>
    [Fact]
    public void Should_Scrub_The_Temp_Script_Name()
    {
        ScriptPlan plan = CliOutcomeParser.ParseScriptPlan(_failed);

        Assert.DoesNotContain("9f3c", string.Join("\n", plan.Diagnostics.Select(d => d.ToString())));
        Assert.Equal("Line 2:3 — unknown directive '@crp' [E_UNKNOWN_DIRECTIVE]", plan.Errors.First().ToString());
    }

    [Theory]
    [InlineData("")]
    [InlineData("not json")]
    [InlineData("[1,2]")]
    public void Should_Refuse_Anything_That_Is_Not_An_Envelope(string stdout) =>
        Assert.Throws<StencilCliException>(() => CliOutcomeParser.ParseScriptPlan(stdout));
}
