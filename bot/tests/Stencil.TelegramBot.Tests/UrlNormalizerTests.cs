using Stencil.TelegramBot.Infrastructure.Server;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Origin normalisation for <see cref="UrlNormalizer"/> — a port of <c>pystencil</c>'s
/// <c>normalize_url</c> tests (itself a port of the browser <c>normalizeUrl</c>).
/// </summary>
public sealed class UrlNormalizerTests
{
    [Fact]
    public void AddsDefaultScheme()
    {
        Assert.Equal("http://host:8090", UrlNormalizer.Normalize("host:8090"));
    }

    [Fact]
    public void StripsPathAndTrailingSlash()
    {
        Assert.Equal("http://host:8090", UrlNormalizer.Normalize("http://host:8090/"));
        Assert.Equal("http://host:8090", UrlNormalizer.Normalize("http://host:8090/projects/x"));
    }

    [Fact]
    public void PreservesHttpsAndPort()
    {
        Assert.Equal("https://example.com:8443", UrlNormalizer.Normalize("https://example.com:8443/api"));
    }

    [Fact]
    public void DropsQueryAndFragment()
    {
        Assert.Equal("http://h:9", UrlNormalizer.Normalize("http://h:9/p?q=1#frag"));
    }

    [Fact]
    public void TrimsWhitespace()
    {
        Assert.Equal("http://example.com", UrlNormalizer.Normalize("  example.com  "));
    }

    [Fact]
    public void EmptyOrNullRaises()
    {
        Assert.Throws<ArgumentException>(() => UrlNormalizer.Normalize(""));
        Assert.Throws<ArgumentException>(() => UrlNormalizer.Normalize("   "));
        Assert.Throws<ArgumentException>(() => UrlNormalizer.Normalize(null));
    }
}
