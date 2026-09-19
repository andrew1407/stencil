using Stencil.TelegramBot.Infrastructure.Server;

namespace Stencil.TelegramBot.Tests;

/// <summary>Origin normalisation — a port of <c>pystencil</c>'s <c>normalize_url</c> tests, itself a port of the browser's <c>normalizeUrl</c>.</summary>
public sealed class UrlNormalizerTests
{
    [Fact]
    public void Should_Default_To_A_Secure_Scheme()
    {
        Assert.Equal("https://host:8090", UrlNormalizer.Normalize("host:8090"));
        Assert.Equal("http://localhost:8090", UrlNormalizer.Normalize("localhost:8090"));
        Assert.Equal("http://127.0.0.1:8090", UrlNormalizer.Normalize("127.0.0.1:8090"));
    }

    [Fact]
    public void Should_Classify_Loopback_Hosts()
    {
        Assert.True(UrlNormalizer.IsLoopbackHost("localhost"));
        Assert.True(UrlNormalizer.IsLoopbackHost("127.0.0.1"));
        Assert.True(UrlNormalizer.IsLoopbackHost("::1"));
        Assert.False(UrlNormalizer.IsLoopbackHost("example.com"));
    }

    [Fact]
    public void Should_Strip_Path_And_Trailing_Slash()
    {
        Assert.Equal("http://host:8090", UrlNormalizer.Normalize("http://host:8090/"));
        Assert.Equal("http://host:8090", UrlNormalizer.Normalize("http://host:8090/projects/x"));
    }

    [Fact]
    public void Should_Preserve_Https_And_Port()
    {
        Assert.Equal("https://example.com:8443", UrlNormalizer.Normalize("https://example.com:8443/api"));
    }

    [Fact]
    public void Should_Drop_Query_And_Fragment()
    {
        Assert.Equal("http://h:9", UrlNormalizer.Normalize("http://h:9/p?q=1#frag"));
    }

    [Fact]
    public void Should_Trim_Whitespace()
    {
        Assert.Equal("https://example.com", UrlNormalizer.Normalize("  example.com  "));
    }

    [Fact]
    public void Should_Raise_On_Empty_Or_Null()
    {
        Assert.Throws<ArgumentException>(() => UrlNormalizer.Normalize(""));
        Assert.Throws<ArgumentException>(() => UrlNormalizer.Normalize("   "));
        Assert.Throws<ArgumentException>(() => UrlNormalizer.Normalize(null));
    }
}
