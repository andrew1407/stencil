using Stencil.TelegramBot.Bot.Telegram;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// <see cref="PageFormats"/> — the bot's ISO 216/269 table, parsed from the canonical
/// <c>browser/js/config/constants.json</c> <c>PAGE_SIZES</c> embedded at build time:
/// canonical order/casing, case-insensitive lookup, and the trimmed cm formatting used
/// in chat text.
/// </summary>
public sealed class PageFormatsTests
{
    [Fact]
    public void TableCoversAllThirtyThreeFormatsInCanonicalOrder()
    {
        Assert.Equal(33, PageFormats.All.Count);
        Assert.Equal("A0", PageFormats.All[0].Name);
        Assert.Equal("B0", PageFormats.All[11].Name);
        Assert.Equal("C10", PageFormats.All[^1].Name);
    }

    [Fact]
    public void EmbeddedTableParsesThirtyThreeFormatsWithA4Dimensions()
    {
        // Fails fast if the build-time embed of constants.json breaks or goes stale.
        Assert.Equal(33, PageFormats.All.Count);
        (string name, double w, double h) = PageFormats.All[4];
        Assert.Equal("A4", name);
        Assert.Equal(21, w);
        Assert.Equal(29.7, h);
    }

    [Fact]
    public void EmbeddedConstantsMatchCanonicalFileBytes()
    {
        // The embed copies the file at build time; catch drift against the repo's canonical copy.
        using Stream? stream = typeof(PageFormats).Assembly.GetManifestResourceStream(
            "Stencil.TelegramBot.Bot.Assets.constants.json");
        Assert.NotNull(stream);
        using var embedded = new MemoryStream();
        stream.CopyTo(embedded);
        byte[] canonical = File.ReadAllBytes(
            SharedFixtures.PathOf("browser", "js", "config", "constants.json"));
        Assert.Equal(canonical, embedded.ToArray());
    }

    [Theory]
    [InlineData("A4", "A4", 21, 29.7)]
    [InlineData("b5", "B5", 17.6, 25)]
    [InlineData("c10", "C10", 2.8, 4)]
    public void TryGetResolvesNamesCaseInsensitively(string given, string canonical, double w, double h)
    {
        Assert.True(PageFormats.TryGet(given, out string name, out double wcm, out double hcm));
        Assert.Equal(canonical, name);
        Assert.Equal(w, wcm);
        Assert.Equal(h, hcm);
    }

    [Theory]
    [InlineData("A11")]
    [InlineData("D4")]
    [InlineData("custom")]
    [InlineData("")]
    public void TryGetRejectsUnknownNames(string given)
    {
        Assert.False(PageFormats.TryGet(given, out _, out _, out _));
    }

    [Fact]
    public void CmTrimsTrailingZeros()
    {
        Assert.Equal("100", PageFormats.Cm(100));
        Assert.Equal("29.7", PageFormats.Cm(29.7));
        Assert.Equal("2.8", PageFormats.Cm(2.80));
    }
}
