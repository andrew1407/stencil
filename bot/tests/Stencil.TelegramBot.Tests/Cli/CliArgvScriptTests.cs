using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests.Cli;

/// <summary><c>--script-plan</c> and <c>--plan-check</c> argv (cli/CONTRACT.md §4.3, §7): plan mode fetches, decodes and writes nothing, so — alone among this adapter's spawns — it carries no <c>--confine-output</c>.</summary>
public sealed class CliArgvScriptTests
{
    [Fact]
    public void Should_Name_Only_The_Script_Leaf()
    {
        IReadOnlyList<string> argv = CliArgvBuilder.BuildScriptPlanArgv("script-abc.stc");

        Assert.Equal(["--script-plan", "script-abc.stc", "--plan-surface", "bot"], argv);
    }

    /// <summary>The frame the script's % lengths resolve against leads, the way -i always does.</summary>
    [Fact]
    public void Should_Put_The_Probe_Input_Before_The_Script()
    {
        IReadOnlyList<string> argv = CliArgvBuilder.BuildScriptPlanArgv("s.stc", "frame.png");

        Assert.Equal(["-i", "frame.png", "--script-plan", "s.stc", "--plan-surface", "bot"], argv);
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

    /// <summary>The model's reply rides stdin: argv holds no text a model wrote and no path.</summary>
    [Fact]
    public void Should_Judge_A_Plan_From_Stdin_Under_The_Bot_Surface() =>
        Assert.Equal(["--plan-check", "-", "--plan-surface", "bot"], CliArgvBuilder.BuildPlanCheckArgv());

    [Fact]
    public void Should_Drop_An_Absent_Input()
    {
        Assert.Equal(["--script-plan", "s.stc", "--plan-surface", "bot"], CliArgvBuilder.BuildScriptPlanArgv("s.stc", null));
        Assert.Equal(["--script-plan", "s.stc", "--plan-surface", "bot"], CliArgvBuilder.BuildScriptPlanArgv("s.stc", ""));
    }
}
