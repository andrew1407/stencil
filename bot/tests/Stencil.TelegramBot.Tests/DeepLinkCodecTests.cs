using Stencil.TelegramBot.Infrastructure.Links;
using Xunit;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The Telegram start-payload codec.
/// GOLDEN VECTORS — duplicated verbatim in browser/tests/deepLink.test.js and
/// desktop/tests/deepLink.headless.cpp. Keep the three in sync.
/// </summary>
public class DeepLinkCodecTests
{
    public static TheoryData<string, string, string> GoldenVectors() => new()
    {
        // loopback keeps http by default → scheme dropped, host|id encoded
        { "localhost:8090", "p_1a2b3c_x1", "1bG9jYWxob3N0OjgwOTB8cF8xYTJiM2NfeDE" },
        // bare remote host defaults to https → scheme dropped
        { "stencil.example.com", "p_1a2b3c_x1", "1c3RlbmNpbC5leGFtcGxlLmNvbXxwXzFhMmIzY194MQ" },
        // explicit http on a remote host is NOT the default → full origin kept
        { "http://stencil.example.com", "p_1", "1aHR0cDovL3N0ZW5jaWwuZXhhbXBsZS5jb218cF8x" },
        // https on a remote host IS the default → dropped, port kept
        { "https://stencil.example.com:8443", "p_1", "1c3RlbmNpbC5leGFtcGxlLmNvbTo4NDQzfHBfMQ" },
        // 47 plaintext bytes → exactly 64 payload chars (the boundary)
        { "https://hoooooooooooooooooooooooooooooooooooooooooo", "p_1",
          "1aG9vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb29vb3xwXzE" },
    };

    [Theory]
    [MemberData(nameof(GoldenVectors))]
    public void Encode_MatchesGoldenVectors(string url, string id, string expected)
    {
        string? payload = DeepLinkCodec.Encode(url, id);
        Assert.Equal(expected, payload);
        Assert.True(payload!.Length <= DeepLinkCodec.TelegramStartLimit);
        Assert.Matches("^1[A-Za-z0-9_-]+$", payload);
    }

    [Fact]
    public void Encode_ReturnsNullPastTheLimit()
    {
        // 48 plaintext bytes → 65 payload chars → overflow
        string host = "https://h" + new string('o', 43);
        Assert.Null(DeepLinkCodec.Encode(host, "p_1"));
    }

    [Theory]
    [MemberData(nameof(GoldenVectors))]
    public void Decode_RoundTripsEveryVector(string url, string id, string payload)
    {
        Assert.True(DeepLinkCodec.TryDecode(payload, out string decodedUrl, out string decodedId));
        // Decoding re-normalizes, so the origin equals the normalized input.
        Assert.Equal(Infrastructure.Server.UrlNormalizer.Normalize(url), decodedUrl);
        Assert.Equal(id, decodedId);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("1")]                       // marker only
    [InlineData("2bG9jYWxob3N0")]           // unknown version marker
    [InlineData("1!!!!")]                   // non-base64url chars
    [InlineData("1cF8x")]                   // decodes to "p_1" — no pipe separator
    [InlineData("1fHBfMQ")]                 // decodes to "|p_1" — empty host
    [InlineData("1bG9jYWxob3N0fA")]         // decodes to "localhost|" — empty id
    public void Decode_RejectsMalformedPayloads(string? payload)
    {
        Assert.False(DeepLinkCodec.TryDecode(payload, out _, out _));
    }

    [Fact]
    public void Decode_RejectsOverlongPayloads()
    {
        string tooLong = "1" + new string('A', DeepLinkCodec.TelegramStartLimit);
        Assert.False(DeepLinkCodec.TryDecode(tooLong, out _, out _));
    }
}
