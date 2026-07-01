using System.Net;
using Stencil.TelegramBot.Application.Editing;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// SSRF guard for user-supplied <c>/url</c> image links: only public http(s) URLs are allowed.
/// All cases here are offline — IP-literal hosts and scheme/shape checks need no DNS.
/// </summary>
public sealed class RemoteImageUrlTests
{
    [Theory]
    [InlineData("ftp://example.com/a.png")]     // non-http scheme
    [InlineData("file:///etc/passwd")]          // local file scheme
    [InlineData("/etc/passwd.png")]             // bare local path (not absolute URL)
    [InlineData("relative/clip.mp4")]           // bare relative path
    [InlineData("")]                            // empty
    [InlineData("   ")]                         // whitespace
    public void ParseRejectsNonHttpSources(string raw)
    {
        Assert.Throws<InvalidOperationException>(() => RemoteImageUrl.Parse(raw));
    }

    [Theory]
    [InlineData("http://example.com/a.png")]
    [InlineData("https://example.com/a.png")]
    [InlineData("https://1.1.1.1/a.png")]
    public void ParseAcceptsHttpUrls(string raw)
    {
        Uri uri = RemoteImageUrl.Parse(raw);
        Assert.True(uri.Scheme is "http" or "https");
    }

    [Theory]
    [InlineData("127.0.0.1")]           // loopback
    [InlineData("10.0.0.5")]            // private
    [InlineData("172.16.9.9")]          // private
    [InlineData("192.168.1.1")]         // private
    [InlineData("169.254.169.254")]     // link-local / cloud metadata
    [InlineData("100.100.0.1")]         // carrier-grade NAT
    [InlineData("0.0.0.0")]             // unspecified
    [InlineData("224.0.0.1")]           // multicast
    [InlineData("::1")]                 // IPv6 loopback
    [InlineData("fe80::1")]             // IPv6 link-local
    [InlineData("fc00::1")]             // IPv6 unique-local
    [InlineData("::ffff:127.0.0.1")]    // IPv4-mapped loopback
    public void IsBlockedAddressFlagsPrivateAndLocal(string ip)
    {
        Assert.True(RemoteImageUrl.IsBlockedAddress(IPAddress.Parse(ip)));
    }

    [Theory]
    [InlineData("1.1.1.1")]
    [InlineData("8.8.8.8")]
    [InlineData("93.184.216.34")]
    [InlineData("2606:4700:4700::1111")]
    public void IsBlockedAddressAllowsPublic(string ip)
    {
        Assert.False(RemoteImageUrl.IsBlockedAddress(IPAddress.Parse(ip)));
    }

    [Theory]
    [InlineData("http://127.0.0.1/secret.png")]
    [InlineData("https://169.254.169.254/latest/meta-data/")]
    [InlineData("http://[::1]/x.png")]
    public async Task ValidateRejectsLiteralInternalHosts(string raw)
    {
        await Assert.ThrowsAsync<InvalidOperationException>(() => RemoteImageUrl.ValidateAsync(raw));
    }

    [Fact]
    public async Task ValidateAllowsPublicLiteralHost()
    {
        await RemoteImageUrl.ValidateAsync("https://1.1.1.1/a.png"); // no throw
    }

    [Fact]
    public async Task ValidateRejectsUnresolvableHostWithoutHanging()
    {
        // A guaranteed-non-resolvable host (.invalid, RFC 2606) with a near-zero resolve budget:
        // whether it times out or fails to resolve, it must surface as InvalidOperationException
        // rather than stall the caller.
        string url = $"https://does-not-exist-{Guid.NewGuid():N}.invalid/a.png";
        await Assert.ThrowsAsync<InvalidOperationException>(
            () => RemoteImageUrl.ValidateAsync(url, resolveTimeout: TimeSpan.FromMilliseconds(1)));
    }
}
