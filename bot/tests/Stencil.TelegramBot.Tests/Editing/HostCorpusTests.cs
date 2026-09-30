using System.Net;
using System.Text.Json;
using Stencil.TelegramBot.Application.Editing;

namespace Stencil.TelegramBot.Tests.Editing;

/// <summary>Walks <c>common/fixtures/net/hosts.json</c>: each host is read out of a URL the way <c>RemoteImageUrl</c> reads it, then judged by every policy variant of the embedded address table and by the guard's own entry points. Literal hosts only, so nothing resolves.</summary>
public sealed class HostCorpusTests
{
    private static readonly string _path = Path.Combine(SharedFixtures.ConfigFixtureDir("net"), "hosts.json");

    public static TheoryData<string> Names => SharedFixtures.TheoryNames(SharedFixtures.CaseNames(_path));

    [Theory]
    [MemberData(nameof(Names))]
    public async Task Should_Read_And_Judge_Each_Host_As_The_Corpus_Expects(string name)
    {
        using JsonDocument doc = SharedFixtures.Case(_path, name);
        JsonElement c = doc.RootElement;
        string host = c.GetProperty("host").GetString()!;
        string url = $"http://{urlHost(host)}/x.png";
        Assert.True(Uri.TryCreate(url, UriKind.Absolute, out Uri? uri), url);
        bool literal = IPAddress.TryParse(uri!.Host, out IPAddress? address);
        if (!c.TryGetProperty("expect", out JsonElement expect))
        {
            Assert.False(literal, $"{host} is a name, not an address");
            return;
        }
        Assert.True(literal, $"{host} → {uri.Host} did not parse as an address");
        Assert.Equal(IPAddress.Parse(c.GetProperty("address").GetString()!).GetAddressBytes(), address!.GetAddressBytes());

        Assert.Equal(verdict(expect, "fetch"), AddressRanges.Blocks(AddressRanges.FETCH, address));
        Assert.Equal(verdict(expect, "fetch+allowLoopback"),
            AddressRanges.Blocks(AddressRanges.FETCH, address, AddressRanges.ALLOW_LOOPBACK));
        Assert.Equal(verdict(expect, "serverTarget"), AddressRanges.Blocks(AddressRanges.SERVER_TARGET, address));
        Assert.Equal(verdict(expect, "serverTarget+allowPrivate"),
            AddressRanges.Blocks(AddressRanges.SERVER_TARGET, address, AddressRanges.ALLOW_PRIVATE));

        Assert.Equal(verdict(expect, "fetch"), RemoteImageUrl.IsBlockedAddress(address));
        Assert.Equal(verdict(expect, "serverTarget"), RemoteImageUrl.ServerAddressGuard(allowPrivate: false)(address));
        Assert.Equal(verdict(expect, "serverTarget+allowPrivate"), RemoteImageUrl.ServerAddressGuard(allowPrivate: true)(address));
        Exception? refused = await Record.ExceptionAsync(() => RemoteImageUrl.ValidateAsync(url));
        Assert.Equal(verdict(expect, "fetch"), refused is InvalidOperationException);
    }

    private static bool verdict(JsonElement expect, string variant) =>
        expect.GetProperty(variant).GetString() == "block";

    // An unbracketed IPv6 host is bracketed, and a zone ID's % is written %25, as a URL spells them.
    private static string urlHost(string host)
    {
        string bare = host.Trim('[', ']');
        return (bare.Contains(':') ? $"[{bare}]" : bare).Replace("%", "%25");
    }
}
