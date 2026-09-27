using System.Net;
using Stencil.TelegramBot.Application.Editing;

namespace Stencil.TelegramBot.Tests.Servers;

/// <summary>SSRF guard for the user-supplied <c>/connect</c> URL: unlike the <c>/url</c> image guard, loopback and private-LAN targets are intentionally ALLOWED (a local or LAN collaboration server is supported) and only link-local / cloud-metadata, unspecified and multicast are blocked.</summary>
public sealed class ServerUrlGuardTests
{
    [Theory]
    [InlineData("169.254.169.254")]     // link-local / cloud metadata
    [InlineData("169.254.1.1")]         // link-local
    [InlineData("0.0.0.0")]             // unspecified
    [InlineData("224.0.0.1")]           // multicast
    [InlineData("fe80::1")]             // IPv6 link-local
    [InlineData("::ffff:169.254.169.254")] // IPv4-mapped link-local
    [InlineData("64:ff9b::a9fe:a9fe")]  // NAT64 of the metadata endpoint
    [InlineData("2002:a9fe:a9fe::")]    // 6to4 of the metadata endpoint
    [InlineData("::169.254.169.254")]   // IPv4-compatible metadata endpoint
    public void Should_Block_Metadata_And_Link_Local_In_Is_Cloud_Metadata_Or_Link_Local(string ip)
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
    [InlineData("64:ff9b::a00:5")]      // NAT64 of a private server
    public void Should_Allow_Loopback_Private_And_Public_In_Is_Cloud_Metadata_Or_Link_Local(string ip)
    {
        Assert.False(RemoteImageUrl.IsCloudMetadataOrLinkLocal(IPAddress.Parse(ip)));
    }

    [Theory]
    [InlineData("http://169.254.169.254")]
    [InlineData("http://169.254.169.254:8090")]
    [InlineData("169.254.169.254")]                     // bare host, no scheme
    [InlineData("https://169.254.169.254/latest/meta-data/")]
    [InlineData("http://[fe80::1]:8090")]
    public async Task Should_Reject_Link_Local_Hosts_On_Validate_Server_Url(string raw)
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
    public async Task Should_Allow_Loopback_And_Private_Hosts_On_Validate_Server_Url(string raw)
    {
        await RemoteImageUrl.ValidateServerUrlAsync(raw); // no throw
    }

    [Theory]
    [InlineData("http://127.0.0.1:8090")]
    [InlineData("localhost")]
    [InlineData("http://192.168.1.50:8090")]
    [InlineData("http://[fc00::1]:8090")]
    [InlineData("http://[64:ff9b::a00:5]")]     // NAT64 of 10.0.0.5
    public async Task Should_Refuse_Private_Hosts_When_The_Operator_Allows_Only_Public_Servers(string raw)
    {
        InvalidOperationException ex = await Assert.ThrowsAsync<InvalidOperationException>(
            () => RemoteImageUrl.ValidateServerUrlAsync(raw, allowPrivate: false));
        Assert.Equal(RemoteImageUrl.PRIVATE_SERVER_REFUSAL, ex.Message);
    }

    [Fact]
    public async Task Should_Still_Allow_A_Public_Host_When_The_Operator_Allows_Only_Public_Servers()
    {
        await RemoteImageUrl.ValidateServerUrlAsync("https://1.1.1.1:8090", allowPrivate: false); // no throw
    }

    [Theory]
    [InlineData(true, "10.0.0.5", false)]
    [InlineData(true, "169.254.169.254", true)]
    [InlineData(false, "10.0.0.5", true)]
    [InlineData(false, "127.0.0.1", true)]
    [InlineData(false, "1.1.1.1", false)]
    public void Should_Dial_Only_What_Validation_Would_Allow(bool allowPrivate, string ip, bool blocked)
    {
        Assert.Equal(blocked, RemoteImageUrl.ServerAddressGuard(allowPrivate)(IPAddress.Parse(ip)));
    }

    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    public async Task Should_Reject_Empty_Input_On_Validate_Server_Url(string raw)
    {
        await Assert.ThrowsAsync<InvalidOperationException>(
            () => RemoteImageUrl.ValidateServerUrlAsync(raw));
    }

    [Fact]
    public async Task Should_Allow_An_Unresolvable_Host_Without_Blocking_On_Validate_Server_Url()
    {
        // Unlike the /url guard, a host that won't resolve is not an SSRF target: it can't be reached, so the
        // connection proceeds and fails on its own.
        string url = $"http://does-not-exist-{Guid.NewGuid():N}.invalid:8090";
        await RemoteImageUrl.ValidateServerUrlAsync(url, resolveTimeout: TimeSpan.FromMilliseconds(1)); // no throw
    }
}
