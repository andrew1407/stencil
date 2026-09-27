using System.Net;
using Stencil.TelegramBot.Application.Editing;

namespace Stencil.TelegramBot.Tests.Editing;

/// <summary>SSRF guard for user-supplied <c>/url</c> image links: only public http(s) URLs are allowed. Every case here is offline — IP-literal hosts and scheme/shape checks need no DNS.</summary>
public sealed class RemoteImageUrlTests
{
    [Theory]
    [InlineData("ftp://example.com/a.png")]     // non-http scheme
    [InlineData("file:///etc/passwd")]          // local file scheme
    [InlineData("/etc/passwd.png")]             // bare local path (not absolute URL)
    [InlineData("relative/clip.mp4")]           // bare relative path
    [InlineData("")]                            // empty
    [InlineData("   ")]                         // whitespace
    public void Should_Reject_Non_Http_Sources_On_Parse(string raw)
    {
        Assert.Throws<InvalidOperationException>(() => RemoteImageUrl.Parse(raw));
    }

    [Theory]
    [InlineData("http://example.com/a.png")]
    [InlineData("https://example.com/a.png")]
    [InlineData("https://1.1.1.1/a.png")]
    public void Should_Accept_Http_Urls_On_Parse(string raw)
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
    [InlineData("198.18.0.1")]          // benchmarking 198.18.0.0/15
    [InlineData("198.19.255.254")]      // …its upper half
    [InlineData("192.0.0.8")]           // IETF protocol assignments 192.0.0.0/24
    [InlineData("255.255.255.255")]     // broadcast
    [InlineData("64:ff9b::7f00:1")]     // NAT64 of 127.0.0.1
    [InlineData("64:ff9b::a9fe:a9fe")]  // NAT64 of the metadata endpoint
    [InlineData("64:ff9b::c0a8:101")]   // NAT64 of 192.168.1.1
    [InlineData("64:ff9b:1::1")]        // local-use NAT64 64:ff9b:1::/48
    [InlineData("2002:7f00:1::")]       // 6to4 of 127.0.0.1
    [InlineData("2002:a9fe:a9fe::1")]   // 6to4 of the metadata endpoint
    [InlineData("2002:c612:1::")]       // 6to4 of 198.18.0.1
    [InlineData("::127.0.0.1")]         // IPv4-compatible loopback
    [InlineData("::a9fe:a9fe")]         // IPv4-compatible metadata endpoint
    [InlineData("::2")]                 // IPv4-compatible 0.0.0.2
    [InlineData("::ffff:198.18.0.1")]   // IPv4-mapped benchmarking
    [InlineData("2001::1")]             // Teredo 2001::/32
    [InlineData("100::1")]              // discard-only 100::/64
    [InlineData("fec0::1")]             // deprecated site-local
    [InlineData("ff02::1")]             // IPv6 multicast
    public void Should_Flag_Private_And_Local_Addresses_As_Blocked(string ip)
    {
        Assert.True(RemoteImageUrl.IsBlockedAddress(IPAddress.Parse(ip)));
    }

    [Theory]
    [InlineData("1.1.1.1")]
    [InlineData("8.8.8.8")]
    [InlineData("93.184.216.34")]
    [InlineData("2606:4700:4700::1111")]
    [InlineData("198.20.0.1")]          // just past 198.18.0.0/15
    [InlineData("192.0.1.1")]           // just past 192.0.0.0/24
    [InlineData("64:ff9b::808:808")]    // NAT64 of a public address
    [InlineData("2002:808:808::1")]     // 6to4 of a public address
    [InlineData("::8.8.8.8")]           // IPv4-compatible public address
    [InlineData("2001:4860:4860::8888")] // public, beside Teredo's 2001::/32
    public void Should_Allow_Public_Addresses_As_Not_Blocked(string ip)
    {
        Assert.False(RemoteImageUrl.IsBlockedAddress(IPAddress.Parse(ip)));
    }

    [Theory]
    [InlineData("64:ff9b::a9fe:a9fe", "169.254.169.254")]
    [InlineData("2002:a9fe:a9fe::1", "169.254.169.254")]
    [InlineData("::ffff:10.0.0.1", "10.0.0.1")]
    [InlineData("::10.0.0.1", "10.0.0.1")]
    [InlineData("::1", "::1")]          // loopback stays IPv6, not 0.0.0.1
    [InlineData("::", "::")]
    [InlineData("2606:4700::1", "2606:4700::1")]
    public void Should_Judge_An_Ipv6_Form_By_The_Ipv4_Address_It_Carries(string ip, string carried)
    {
        Assert.Equal(IPAddress.Parse(carried), AddressRanges.Carried(IPAddress.Parse(ip)));
    }

    [Theory]
    [InlineData("http://127.0.0.1/secret.png")]
    [InlineData("http://[64:ff9b::a9fe:a9fe]/latest/meta-data/")]
    [InlineData("http://198.18.0.1/x.png")]
    [InlineData("https://169.254.169.254/latest/meta-data/")]
    [InlineData("http://[::1]/x.png")]
    public async Task Should_Reject_Literal_Internal_Hosts_On_Validate(string raw)
    {
        await Assert.ThrowsAsync<InvalidOperationException>(() => RemoteImageUrl.ValidateAsync(raw));
    }

    [Fact]
    public async Task Should_Allow_A_Public_Literal_Host_On_Validate()
    {
        await RemoteImageUrl.ValidateAsync("https://1.1.1.1/a.png"); // no throw
    }

    [Fact]
    public async Task Should_Reject_An_Unresolvable_Host_Without_Hanging_On_Validate()
    {
        // .invalid is guaranteed non-resolvable (RFC 2606); a timeout and a resolve failure must both surface as
        // InvalidOperationException rather than stall the caller.
        string url = $"https://does-not-exist-{Guid.NewGuid():N}.invalid/a.png";
        await Assert.ThrowsAsync<InvalidOperationException>(
            () => RemoteImageUrl.ValidateAsync(url, resolveTimeout: TimeSpan.FromMilliseconds(1)));
    }
}
