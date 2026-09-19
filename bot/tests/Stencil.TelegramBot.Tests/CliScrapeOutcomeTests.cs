using System.Runtime.CompilerServices;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests;

/// <summary><c>ParseScraped</c> over a source-site run's multi-file stderr (source-site contract §3), pinned against the shared <c>cli/testdata/scrape_fixtures.json</c> corpus.</summary>
public sealed class CliScrapeOutcomeTests
{
    [Fact]
    public void Should_Parse_Two_Measured_Images()
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
    public void Should_Give_A_Video_Line_Null_Dimensions()
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
    public void Should_Still_Parse_A_Scraped_Path_Containing_A_Paren()
    {
        ScrapeResult result = CliOutcomeParser.ParseScraped(
            "wrote out/img (1) (300x300 px · source example.org)\n"
            + "scraped 1 file(s) from example.org into out");
        ScrapedFile only = Assert.Single(result.Files);
        Assert.Equal("out/img (1)", only.Path);
        Assert.Equal((300, 300), (only.Width, only.Height));
    }

    [Fact]
    public void Should_Parse_No_Media_Stderr_To_No_Files_And_Surface_The_Error()
    {
        const string stderr = "error: no media matched at https://example.com/";
        Assert.Empty(CliOutcomeParser.ParseScraped(stderr).Files);
        Assert.Equal("error: no media matched at https://example.com/", CliOutcomeParser.ExtractErrors(stderr));
    }

    /// <summary>The shared <c>cli/testdata/scrape_fixtures.json</c> goldens: the CLI must emit these exact line shapes and this parser must recover the files/dirs, so a drift on either side goes red.</summary>
    [Fact]
    public void Should_Match_The_Scrape_Golden_Fixtures()
    {
        string json = File.ReadAllText(scrapeFixturesPath());
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
                Assert.Equal(nullableInt(ef.GetProperty("width")), got.Files[i].Width);
                Assert.Equal(nullableInt(ef.GetProperty("height")), got.Files[i].Height);
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

    private static int? nullableInt(JsonElement e) =>
        e.ValueKind == JsonValueKind.Null ? null : e.GetInt32();

    /// <summary>Locate the shared fixtures relative to THIS test source (compile-time path).</summary>
    private static string scrapeFixturesPath([CallerFilePath] string thisFile = "")
    {
        string dir = Path.GetDirectoryName(thisFile)!;
        // .../bot/tests/Stencil.TelegramBot.Tests -> repo root is three levels up.
        return Path.GetFullPath(
            Path.Combine(dir, "..", "..", "..", "cli", "testdata", "scrape_fixtures.json"));
    }
}
