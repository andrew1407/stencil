using System.Text.Json;
using Stencil.TelegramBot.Infrastructure.Links;
using Stencil.TelegramBot.Infrastructure.Server;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Walks the shared <c>?start=</c> deep-link golden vectors
/// (<c>browser/js/config/fixtures/deepLink/telegramStart.json</c>, see <c>_schema.md</c>)
/// through <see cref="DeepLinkCodec"/> — the same six vectors the literal-based
/// <see cref="DeepLinkCodecTests"/> pin; this walker is additive and reads them from the
/// shared corpus instead. Encode must match byte-for-byte (null on overflow); as a decoding
/// surface the bot also round-trips each payload back to (normalized origin, id).
/// (launchPayload.json targets <c>normalizeLaunchPayload</c>, which the bot does not
/// implement — not walked here.)
/// </summary>
public sealed class DeepLinkFixtureWalkerTests
{
    [Fact]
    public void EveryTelegramStartVectorMatchesAndRoundTrips()
    {
        List<string> failures = new();
        int walked = 0;
        using JsonDocument doc = SharedFixtures.Load(
            Path.Combine(SharedFixtures.ConfigFixtureDir("deepLink"), "telegramStart.json"));
        foreach (JsonElement fx in doc.RootElement.EnumerateArray())
        {
            walked++;
            string name = fx.GetProperty("name").GetString()!;
            string serverUrl = fx.GetProperty("serverUrl").GetString()!;
            string projectId = fx.GetProperty("projectId").GetString()!;
            JsonElement expect = fx.GetProperty("expectPayload");

            string? payload = DeepLinkCodec.Encode(serverUrl, projectId);
            if (expect.ValueKind == JsonValueKind.Null)
            {
                if (payload is not null)
                {
                    failures.Add($"{name}: expected null (overflow), got \"{payload}\"");
                }
                continue;
            }
            if (payload != expect.GetString())
            {
                failures.Add($"{name}: \"{payload}\" != \"{expect.GetString()}\"");
                continue;
            }
            if (!DeepLinkCodec.TryDecode(payload, out string decodedUrl, out string decodedId))
            {
                failures.Add($"{name}: TryDecode refused its own encoding");
            }
            else
            {
                if (decodedUrl != UrlNormalizer.Normalize(serverUrl))
                {
                    failures.Add($"{name}: decoded url \"{decodedUrl}\" != \"{UrlNormalizer.Normalize(serverUrl)}\"");
                }
                if (decodedId != projectId)
                {
                    failures.Add($"{name}: decoded id \"{decodedId}\" != \"{projectId}\"");
                }
            }
        }
        Assert.Equal(6, walked);
        Assert.True(failures.Count == 0,
            $"{failures.Count} deep-link mismatches (walked {walked}):\n" + string.Join("\n", failures));
    }
}
