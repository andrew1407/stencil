using System.Net;
using Stencil.TelegramBot.Application.Editing;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// SSRF guard for the user-supplied <c>/connect</c> server URL. Unlike the <c>/url</c> image
/// guard, loopback and private-LAN targets are intentionally ALLOWED (connecting to a local or
/// LAN collaboration server is a supported feature); only the link-local / cloud-metadata range
/// (and unspecified/multicast) is blocked. All cases here are offline — IP literals and
/// <c>localhost</c> need no network DNS.
/// </summary>
public sealed class ServerUrlGuardTests
{
    [Theory]
    [InlineData("169.254.169.254")]     // link-local / cloud metadata
    [InlineData("169.254.1.1")]         // link-local
    [InlineData("0.0.0.0")]             // unspecified
    [InlineData("224.0.0.1")]           // multicast
    [InlineData("fe80::1")]             // IPv6 link-local
    [InlineData("::ffff:169.254.169.254")] // IPv4-mapped link-local
    public void IsCloudMetadataOrLinkLocalBlocksMetadataAndLinkLocal(string ip)
    {
        Assert.True(RemoteImageUrl.IsCloudMetadataOrLinkLocal(IPAddress.Parse(ip)));
    }

    [Theory]
    [InlineData("127.0.0.1")]           // loopback — allowed server target
    [InlineData("10.0.0.5")]            // private — allowed server target
    [InlineData("172.16.9.9")]          // private — allowed server target
    [InlineData("192.168.1.1")]         // private — allowed server target
    [InlineData("100.100.0.1")]         // carrier-grade NAT — allowed server target
    [InlineData("1.1.1.1")]             // public
    [InlineData("8.8.8.8")]             // public
    [InlineData("::1")]                 // IPv6 loopback
    [InlineData("fc00::1")]             // IPv6 unique-local
    [InlineData("2606:4700:4700::1111")] // public IPv6
    [InlineData("::ffff:127.0.0.1")]    // IPv4-mapped loopback
    public void IsCloudMetadataOrLinkLocalAllowsLoopbackPrivateAndPublic(string ip)
    {
        Assert.False(RemoteImageUrl.IsCloudMetadataOrLinkLocal(IPAddress.Parse(ip)));
    }

    [Theory]
    [InlineData("http://169.254.169.254")]
    [InlineData("http://169.254.169.254:8090")]
    [InlineData("169.254.169.254")]                     // bare host, no scheme
    [InlineData("https://169.254.169.254/latest/meta-data/")]
    [InlineData("http://[fe80::1]:8090")]
    public async Task ValidateServerUrlRejectsLinkLocalHosts(string raw)
    {
        await Assert.ThrowsAsync<InvalidOperationException>(
            () => RemoteImageUrl.ValidateServerUrlAsync(raw));
    }

    [Theory]
    [InlineData("http://127.0.0.1:8090")]       // loopback server — allowed
    [InlineData("http://localhost:8090")]       // localhost — allowed (resolves via hosts file)
    [InlineData("localhost")]                    // bare localhost, no scheme
    [InlineData("http://192.168.1.50:8090")]    // LAN server — allowed
    [InlineData("http://10.1.2.3:8090")]        // private server — allowed
    [InlineData("https://172.16.0.9")]          // private server — allowed
    public async Task ValidateServerUrlAllowsLoopbackAndPrivateHosts(string raw)
    {
        await RemoteImageUrl.ValidateServerUrlAsync(raw); // no throw
    }

    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    public async Task ValidateServerUrlRejectsEmptyInput(string raw)
    {
        await Assert.ThrowsAsync<InvalidOperationException>(
            () => RemoteImageUrl.ValidateServerUrlAsync(raw));
    }

    [Fact]
    public async Task ValidateServerUrlAllowsUnresolvableHostWithoutBlocking()
    {
        // Unlike the /url guard, a host that won't resolve is not treated as an SSRF target: it
        // can't be reached, so the connection is allowed to proceed and fail on its own. A
        // near-zero resolve budget makes this fast even offline.
        string url = $"http://does-not-exist-{Guid.NewGuid():N}.invalid:8090";
        await RemoteImageUrl.ValidateServerUrlAsync(url, resolveTimeout: TimeSpan.FromMilliseconds(1)); // no throw
    }
}
