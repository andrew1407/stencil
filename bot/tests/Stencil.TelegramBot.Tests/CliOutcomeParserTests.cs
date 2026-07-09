using System.Runtime.CompilerServices;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Parsing the CLI's stderr into structured results — a port of <c>mcp/tests/outcome_test.rs</c>,
/// covering <c>ParseWrote</c>, <c>ParseRemotes</c> (the collaboration-server delivery lines),
/// and <c>ExtractErrors</c> per <c>cli/CONTRACT.md</c> §2.
/// </summary>
public sealed class CliOutcomeParserTests
{
    [Fact]
    public void ParsesSuccessLine()
    {
        RenderResult? result = CliOutcomeParser.ParseWrote("wrote /tmp/out.png (800x600)\n");
        Assert.NotNull(result);
        Assert.Equal("/tmp/out.png", result!.Path);
        Assert.Equal((800, 600), (result.Width, result.Height));
    }

    [Fact]
    public void ParsesSuccessLineAmongBannerNoise()
    {
        string stderr = "  ___ stencil banner ___\nsome usage text\nwrote out.png (16x12)\n";
        RenderResult? result = CliOutcomeParser.ParseWrote(stderr);
        Assert.NotNull(result);
        Assert.Equal("out.png", result!.Path);
        Assert.Equal((16, 12), (result.Width, result.Height));
    }

    [Fact]
    public void ParsesRichDimensionsWithPageSuffix()
    {
        // The real CLI appends " px · A4 …" after WxH (the cm size uses '×', not 'x').
        RenderResult? result = CliOutcomeParser.ParseWrote("wrote /tmp/out.png (1280x720 px · A4 29.7×21cm)");
        Assert.NotNull(result);
        Assert.Equal("/tmp/out.png", result!.Path);
        Assert.Equal((1280, 720), (result.Width, result.Height));
    }

    [Fact]
    public void PageSuffixLabelIsInformationalWhateverTheFormat()
    {
        // The page label reflects the page actually used (e.g. a /blank b5) — still ignored here.
        RenderResult? result = CliOutcomeParser.ParseWrote("wrote /tmp/out.png (665x945 px · B5 17.6×25cm)");
        Assert.NotNull(result);
        Assert.Equal((665, 945), (result!.Width, result.Height));
    }

    [Fact]
    public void HandlesPathContainingAParen()
    {
        RenderResult? result = CliOutcomeParser.ParseWrote("wrote /tmp/my (final) shot.png (1920x1080)");
        Assert.NotNull(result);
        Assert.Equal("/tmp/my (final) shot.png", result!.Path);
        Assert.Equal((1920, 1080), (result.Width, result.Height));
    }

    [Fact]
    public void NoSuccessLineReturnsNull()
    {
        Assert.Null(CliOutcomeParser.ParseWrote("error: something went wrong"));
    }

    [Fact]
    public void MalformedDimensionsReturnsNull()
    {
        Assert.Null(CliOutcomeParser.ParseWrote("wrote out.png (widexhigh)"));
        Assert.Null(CliOutcomeParser.ParseWrote("wrote out.png (800-600)"));
    }

    [Fact]
    public void ParsesRemoteUpdateLine()
    {
        IReadOnlyList<RemoteDelivery> remotes = CliOutcomeParser.ParseRemotes(
            "wrote out.png (800x600)\nupdated server result for project p_x_y (800x600)\n");
        RemoteDelivery.Updated updated = Assert.IsType<RemoteDelivery.Updated>(Assert.Single(remotes));
        Assert.Equal("p_x_y", updated.Id);
        Assert.Equal((800, 600), (updated.Width, updated.Height));
    }

    [Fact]
    public void ParsesRemoteCreateLine()
    {
        IReadOnlyList<RemoteDelivery> remotes = CliOutcomeParser.ParseRemotes(
            "wrote out.png (16x12)\ncreated server project \"My Shot\" (p_a_b)\n");
        RemoteDelivery.Created created = Assert.IsType<RemoteDelivery.Created>(Assert.Single(remotes));
        Assert.Equal("My Shot", created.Name);
        Assert.Equal("p_a_b", created.Id);
    }

    [Fact]
    public void ParsesBothServerDeliveriesInOneRun()
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
    public void NoServerLinesYieldsEmpty()
    {
        Assert.Empty(CliOutcomeParser.ParseRemotes("wrote out.png (10x10)"));
    }

    [Fact]
    public void ExtractsErrorLines()
    {
        string stderr = "banner\nusage: stencil ...\nerror: could not parse --crop \"oops\"\n";
        Assert.Equal("error: could not parse --crop \"oops\"", CliOutcomeParser.ExtractErrors(stderr));
    }

    [Fact]
    public void FallsBackToFullStderrWithoutErrorPrefix()
    {
        Assert.Equal("unexpected text", CliOutcomeParser.ExtractErrors("  unexpected text  "));
    }

    [Fact]
    public void EmptyStderrHasAMessage()
    {
        Assert.False(string.IsNullOrEmpty(CliOutcomeParser.ExtractErrors("")));
    }

    // ── ParseScraped: source-site multi-file stderr (DESIGN source-site contract §3) ──

    [Fact]
    public void ParsesTwoMeasuredImages()
    {
        ScrapeResult result = CliOutcomeParser.ParseScraped(
            "wrote out/logo.png (200x80 px · source example.com)\n"
            + "wrote out/hero.jpg (1280x720 px · source example.com)\n"
            + "scraped 2 file(s) from example.com into out");
        Assert.Equal("out", result.Directory);
        Assert.Collection(
            result.Files,
            f => { Assert.Equal("out/logo.png", f.Path); Assert.Equal(200, f.Width); Assert.Equal(80, f.Height); },
            f => { Assert.Equal("out/hero.jpg", f.Path); Assert.Equal(1280, f.Width); Assert.Equal(720, f.Height); });
    }

    [Fact]
    public void VideoLineHasNullDimensions()
    {
        ScrapeResult result = CliOutcomeParser.ParseScraped(
            "wrote assets/pic.png (640x480 px · source cdn.test)\n"
            + "wrote assets/clip.mp4 (source cdn.test)\n"
            + "scraped 2 file(s) from cdn.test into assets");
        Assert.Equal("assets", result.Directory);
        Assert.Equal((640, 480), (result.Files[0].Width, result.Files[0].Height));
        Assert.Null(result.Files[1].Width);
        Assert.Null(result.Files[1].Height);
        Assert.Equal("assets/clip.mp4", result.Files[1].Path);
    }

    [Fact]
    public void ScrapedPathContainingAParenStillParses()
    {
        ScrapeResult result = CliOutcomeParser.ParseScraped(
            "wrote out/img (1) (300x300 px · source example.org)\n"
            + "scraped 1 file(s) from example.org into out");
        ScrapedFile only = Assert.Single(result.Files);
        Assert.Equal("out/img (1)", only.Path);
        Assert.Equal((300, 300), (only.Width, only.Height));
    }

    [Fact]
    public void NoMediaStderrParsesToNoFilesAndSurfacesTheError()
    {
        const string stderr = "error: no media matched at https://example.com/";
        Assert.Empty(CliOutcomeParser.ParseScraped(stderr).Files);
        Assert.Equal("error: no media matched at https://example.com/", CliOutcomeParser.ExtractErrors(stderr));
    }

    /// <summary>
    /// Replay the shared golden fixtures (<c>cli/testdata/scrape_fixtures.json</c>) through
    /// <see cref="CliOutcomeParser.ParseScraped"/>. The CLI must reproduce these exact line shapes
    /// and this parser (like mcp's) must recover the files/dirs, so a drift on either side goes red.
    /// </summary>
    [Fact]
    public void ScrapeGoldenFixturesMatch()
    {
        string json = File.ReadAllText(ScrapeFixturesPath());
        using JsonDocument doc = JsonDocument.Parse(json);
        foreach (JsonElement c in doc.RootElement.GetProperty("cases").EnumerateArray())
        {
            string name = c.GetProperty("name").GetString()!;
            string stderr = c.GetProperty("stderr").GetString()!;
            ScrapeResult got = CliOutcomeParser.ParseScraped(stderr);

            JsonElement expectedFiles = c.GetProperty("files");
            Assert.Equal(expectedFiles.GetArrayLength(), got.Files.Count);
            int i = 0;
            foreach (JsonElement ef in expectedFiles.EnumerateArray())
            {
                Assert.Equal(ef.GetProperty("path").GetString(), got.Files[i].Path);
                Assert.Equal(NullableInt(ef.GetProperty("width")), got.Files[i].Width);
                Assert.Equal(NullableInt(ef.GetProperty("height")), got.Files[i].Height);
                i++;
            }

            // The summary's directory is recovered when present (null for the error case).
            if (c.GetProperty("dir").ValueKind != JsonValueKind.Null)
            {
                Assert.Equal(c.GetProperty("dir").GetString(), got.Directory);
            }

            // The error case has no wrote lines but ExtractErrors still surfaces the message.
            JsonElement error = c.GetProperty("error");
            if (error.ValueKind != JsonValueKind.Null)
            {
                Assert.Contains(error.GetString()!, CliOutcomeParser.ExtractErrors(stderr));
            }
        }
    }

    private static int? NullableInt(JsonElement e) =>
        e.ValueKind == JsonValueKind.Null ? null : e.GetInt32();

    /// <summary>Locate the shared fixtures relative to THIS test source (compile-time path).</summary>
    private static string ScrapeFixturesPath([CallerFilePath] string thisFile = "")
    {
        string dir = Path.GetDirectoryName(thisFile)!;
        // .../bot/tests/Stencil.TelegramBot.Tests -> repo root is three levels up.
        return Path.GetFullPath(
            Path.Combine(dir, "..", "..", "..", "cli", "testdata", "scrape_fixtures.json"));
    }
}
