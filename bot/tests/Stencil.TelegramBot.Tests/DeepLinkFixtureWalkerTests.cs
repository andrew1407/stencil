using System.Text.Json;
using Stencil.TelegramBot.Infrastructure.Links;
using Stencil.TelegramBot.Infrastructure.Server;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The shared <c>?start=</c> vectors through <see cref="DeepLinkCodec"/>, one test per vector:
/// the same six <see cref="DeepLinkCodecTests"/> pin as literals, read from the corpus instead.
/// Encode must match byte-for-byte (null on overflow) and each payload round-trips back to
/// (normalized origin, id). launchPayload.json targets a browser-only helper — not walked.
/// </summary>
public sealed class DeepLinkFixtureWalkerTests
{
    private static string Corpus =>
        Path.Combine(SharedFixtures.ConfigFixtureDir("deepLink"), "telegramStart.json");

    public static TheoryData<string> Vectors() => SharedFixtures.TheoryNames(SharedFixtures.CaseNames(Corpus));

    [Fact]
    public void Should_Have_Every_Vector_In_The_Corpus() => Assert.Equal(6, SharedFixtures.Cases(Corpus).Count);

    [Theory]
    [MemberData(nameof(Vectors))]
    public void Should_Match_And_Round_Trip_Each_Vector(string name)
    {
        using JsonDocument doc = SharedFixtures.Case(Corpus, name);
        JsonElement fx = doc.RootElement;
        string serverUrl = fx.GetProperty("serverUrl").GetString()!;
        string projectId = fx.GetProperty("projectId").GetString()!;
        JsonElement expect = fx.GetProperty("expectPayload");

        string? payload = DeepLinkCodec.Encode(serverUrl, projectId);
        if (expect.ValueKind == JsonValueKind.Null)
        {
            Assert.Null(payload); // overflow
            return;
        }
        Assert.Equal(expect.GetString(), payload);
        Assert.True(DeepLinkCodec.TryDecode(payload!, out string decodedUrl, out string decodedId),
            "TryDecode refused its own encoding");
        Assert.Equal(UrlNormalizer.Normalize(serverUrl), decodedUrl);
        Assert.Equal(projectId, decodedId);
    }
}
