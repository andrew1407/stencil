using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// <c>--script-plan</c> argv (cli/CONTRACT.md §5). Plan mode fetches nothing, decodes nothing and
/// writes nothing, so — unlike every other spawn this adapter makes — it carries no
/// <c>--confine-output</c>; there is no output to confine.
/// </summary>
public sealed class CliArgvScriptTests
{
    [Fact]
    public void Should_Name_Only_The_Script_Leaf()
    {
        IReadOnlyList<string> argv = CliArgvBuilder.BuildScriptPlanArgv("script-abc.stc");

        Assert.Equal(["--script-plan", "script-abc.stc"], argv);
    }

    /// <summary>The frame the script's % lengths resolve against leads, the way -i always does.</summary>
    [Fact]
    public void Should_Put_The_Probe_Input_Before_The_Script()
    {
        IReadOnlyList<string> argv = CliArgvBuilder.BuildScriptPlanArgv("s.stc", "frame.png");

        Assert.Equal(["-i", "frame.png", "--script-plan", "s.stc"], argv);
    }

    [Fact]
    public void Should_Never_Confine_A_Run_That_Writes_Nothing()
    {
        IReadOnlyList<string> argv = CliArgvBuilder.BuildScriptPlanArgv("s.stc", "frame.png");

        Assert.DoesNotContain("--confine-output", argv);
    }

    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    public void Should_Refuse_An_Empty_Script_Name(string script) =>
        Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildScriptPlanArgv(script));

    // The CLI has no `--` terminator, so a dash-leading value would parse as a flag.
    [Fact]
    public void Should_Refuse_A_Dash_Leading_Script_Name() =>
        Assert.Contains("must not start with '-'",
            Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildScriptPlanArgv("-rf.stc")).Message);

    [Fact]
    public void Should_Refuse_A_Dash_Leading_Input() =>
        Assert.Contains("must not start with '-'",
            Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildScriptPlanArgv("s.stc", "-i.png")).Message);

    [Fact]
    public void Should_Drop_An_Absent_Input()
    {
        Assert.Equal(["--script-plan", "s.stc"], CliArgvBuilder.BuildScriptPlanArgv("s.stc", null));
        Assert.Equal(["--script-plan", "s.stc"], CliArgvBuilder.BuildScriptPlanArgv("s.stc", ""));
    }
}
