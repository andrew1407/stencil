using Stencil.TelegramBot.Infrastructure.Server;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Origin normalisation for <see cref="UrlNormalizer"/> — a port of <c>pystencil</c>'s
/// <c>normalize_url</c> tests (itself a port of the browser <c>normalizeUrl</c>).
/// </summary>
public sealed class UrlNormalizerTests
{
    [Fact]
    public void SecureByDefaultScheme()
    {
        Assert.Equal("https://host:8090", UrlNormalizer.Normalize("host:8090"));
        Assert.Equal("http://localhost:8090", UrlNormalizer.Normalize("localhost:8090"));
        Assert.Equal("http://127.0.0.1:8090", UrlNormalizer.Normalize("127.0.0.1:8090"));
    }

    [Fact]
    public void ClassifiesLoopbackHosts()
    {
        Assert.True(UrlNormalizer.IsLoopbackHost("localhost"));
        Assert.True(UrlNormalizer.IsLoopbackHost("127.0.0.1"));
        Assert.True(UrlNormalizer.IsLoopbackHost("::1"));
        Assert.False(UrlNormalizer.IsLoopbackHost("example.com"));
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
        Assert.Equal("https://example.com", UrlNormalizer.Normalize("  example.com  "));
    }

    [Fact]
    public void EmptyOrNullRaises()
    {
        Assert.Throws<ArgumentException>(() => UrlNormalizer.Normalize(""));
        Assert.Throws<ArgumentException>(() => UrlNormalizer.Normalize("   "));
        Assert.Throws<ArgumentException>(() => UrlNormalizer.Normalize(null));
    }
}
