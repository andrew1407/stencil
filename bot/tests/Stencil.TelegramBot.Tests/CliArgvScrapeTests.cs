using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// <c>BuildScrapeArgv</c>'s flag set and ordering (DESIGN source-site contract §1), including
/// the "0 = unset" / "count absent = all" semantics and the output-dir guard.
/// </summary>
public sealed class CliArgvScrapeTests
{
    [Fact]
    public void Should_Build_Scrape_With_Minimal_Url_And_Output_Dir()
    {
        ScrapeRequest req = new() { Url = "https://example.com", OutputDir = "out" };
        Assert.Equal(
            new[] { "--source-site", "https://example.com", "--confine-output", "out" },
            CliArgvBuilder.BuildScrapeArgv(req));
    }

    [Fact]
    public void Should_Build_Scrape_With_All_Flags_In_Contract_Order()
    {
        ScrapeRequest req = new()
        {
            Url = "https://example.com/gallery",
            Count = 6,
            Group = 1,
            Filter = "img|video",
            Format = "png|jpg",
            Name = "cat.*\\.jpg",
            MinWidth = 200,
            MaxWidth = 4000,
            MinHeight = 100,
            MaxHeight = 3000,
            OutputDir = "downloads",
        };
        Assert.Equal(
            new[]
            {
                "--source-site", "https://example.com/gallery",
                "--source-count", "6",
                "--group", "1",
                "--source-filter", "img|video",
                "--source-format", "png|jpg",
                "--source-name", "cat.*\\.jpg",
                "--source-min-width", "200",
                "--source-max-width", "4000",
                "--source-min-height", "100",
                "--source-max-height", "3000",
                "--confine-output", "downloads",
            },
            CliArgvBuilder.BuildScrapeArgv(req));
    }

    [Fact]
    public void Should_Omit_Absent_And_Unset_Bounds_For_Scrape()
    {
        // count/group absent ⇒ omitted; a 0 or negative dimension bound is "unset" ⇒ omitted.
        ScrapeRequest req = new()
        {
            Url = "https://example.com",
            MinWidth = 0,
            MaxWidth = -5,
            MinHeight = 0,
            MaxHeight = 0,
            OutputDir = "out",
        };
        Assert.Equal(
            new[] { "--source-site", "https://example.com", "--confine-output", "out" },
            CliArgvBuilder.BuildScrapeArgv(req));
    }

    [Fact]
    public void Should_Pass_Scrape_Count_Zero_Through_As_All()
    {
        // A count of 0 is a set int, not "absent": it rides through as `--source-count 0`, which
        // the CLI interprets as "all matches" (the /sourcesite handler maps an explicit 0 here).
        ScrapeRequest req = new() { Url = "https://example.com", Count = 0, OutputDir = "out" };
        Assert.Equal(
            new[] { "--source-site", "https://example.com", "--source-count", "0", "--confine-output", "out" },
            CliArgvBuilder.BuildScrapeArgv(req));
    }

    [Fact]
    public void Should_Keep_Scrape_Isolate_One_As_Argv()
    {
        // The /sourceupload isolate-one shape: image-category stills, Count=1, Group=index.
        ScrapeRequest req = new()
        {
            Url = "https://example.com/gallery",
            Filter = "img|background|poster",
            Count = 1,
            Group = 2,
            OutputDir = "out",
        };
        Assert.Equal(
            new[]
            {
                "--source-site", "https://example.com/gallery",
                "--source-count", "1",
                "--group", "2",
                "--source-filter", "img|background|poster",
                "--confine-output", "out",
            },
            CliArgvBuilder.BuildScrapeArgv(req));
    }

    [Fact]
    public void Should_Place_Scrape_Output_Dir_As_The_Positional_Last()
    {
        ScrapeRequest req = new()
        {
            Url = "https://example.com",
            Filter = "background",
            OutputDir = "some/dir",
        };
        IReadOnlyList<string> argv = CliArgvBuilder.BuildScrapeArgv(req);
        Assert.Equal("some/dir", argv[^1]);
    }

    [Fact]
    public void Should_Reject_An_Empty_Url_For_Scrape()
    {
        ScrapeRequest req = new() { Url = "  ", OutputDir = "out" };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildScrapeArgv(req));
        Assert.Contains("url", ex.Message);
    }

    [Fact]
    public void Should_Reject_An_Empty_Output_Dir_For_Scrape()
    {
        ScrapeRequest req = new() { Url = "https://example.com", OutputDir = "" };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildScrapeArgv(req));
        Assert.Contains("output", ex.Message);
    }

    [Theory]
    [InlineData("--source-format")]
    [InlineData("-l")]
    [InlineData("-")]
    public void Should_Reject_A_Dash_Leading_Output_Dir_Without_Flag_Injection_For_Scrape(string badDir)
    {
        // The output dir is the positional operand and the CLI has no `--` terminator, so a
        // dash-leading value would be parsed as a flag. BuildScrapeArgv rejects it outright.
        ScrapeRequest req = new() { Url = "https://example.com", OutputDir = badDir };
        StencilCliException ex = Assert.Throws<StencilCliException>(() => CliArgvBuilder.BuildScrapeArgv(req));
        Assert.Contains("must not start with '-'", ex.Message);
    }
}
