using System.Text.Json;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Walks the shared §12.1 persisted-chat document vectors
/// (<c>browser/js/config/llm/fixtures/chatDoc/</c>, see <c>_schema.md</c>) through
/// <see cref="ChatDocument.TryParse"/>/<see cref="ChatDocument.ToJson"/>:
/// <c>roundtrip.json</c> asserts parse→serialize→parse is the identity (structural JSON
/// compare), <c>tolerance.json</c> asserts the lenient-read pins. The bot parses strings
/// only, so object-form cases are serialized first, per the schema. Two measured
/// divergences are pinned in <c>FixtureOverrides.json</c>.
/// </summary>
public sealed class ChatDocFixtureWalkerTests
{
    [Fact]
    public void RoundtripDocsAreFixedPoints()
    {
        List<string> failures = new();
        int walked = 0;
        using JsonDocument doc = SharedFixtures.Load(
            Path.Combine(SharedFixtures.LlmFixtureDir("chatDoc"), "roundtrip.json"));
        foreach (JsonElement fx in doc.RootElement.EnumerateArray())
        {
            walked++;
            string name = fx.GetProperty("name").GetString()!;
            string raw = fx.GetProperty("doc").GetRawText();

            ChatDocument? parsed = ChatDocument.TryParse(raw);
            if (parsed is null)
            {
                failures.Add($"{name}: TryParse returned null for a canonical document");
                continue;
            }
            JsonNode? expected = JsonNode.Parse(raw);
            JsonNode? reserialized = JsonNode.Parse(parsed.ToJson());
            if (!JsonNode.DeepEquals(reserialized, expected))
            {
                failures.Add($"{name}: serialize∘parse is not the identity\n  got    {reserialized?.ToJsonString()}\n  expect {expected?.ToJsonString()}");
                continue;
            }
            // …and parsing the re-serialized form converges (a true fixed point).
            ChatDocument? again = ChatDocument.TryParse(parsed.ToJson());
            if (again is null || !JsonNode.DeepEquals(JsonNode.Parse(again.ToJson()), expected))
            {
                failures.Add($"{name}: parse(serialize(parse(doc))) diverged");
            }
        }
        Assert.Equal(5, walked);
        Assert.True(failures.Count == 0,
            $"{failures.Count} roundtrip failures:\n" + string.Join("\n", failures));
    }

    [Fact]
    public void TolerantReadsMatchTheirPins()
    {
        List<string> failures = new();
        int walked = 0;
        using JsonDocument doc = SharedFixtures.Load(
            Path.Combine(SharedFixtures.LlmFixtureDir("chatDoc"), "tolerance.json"));
        foreach (JsonElement fx in doc.RootElement.EnumerateArray())
        {
            walked++;
            string name = fx.GetProperty("name").GetString()!;
            // Exactly one of doc (object form — stringify for our string-only parser) / docString.
            string input = fx.TryGetProperty("docString", out JsonElement docString)
                ? docString.GetString()!
                : fx.GetProperty("doc").GetRawText();
            JsonElement expectParsed = SharedFixtures.OverrideFor("chatDocTolerance", name) is JsonElement ov
                ? ov.GetProperty("expectParsed")
                : fx.GetProperty("expectParsed");

            ChatDocument? parsed = ChatDocument.TryParse(input);
            if (expectParsed.ValueKind == JsonValueKind.Null)
            {
                if (parsed is not null)
                {
                    failures.Add($"{name}: expected null (missing document), got {parsed.ToJson()}");
                }
                continue;
            }
            if (parsed is null)
            {
                failures.Add($"{name}: expected a document, got null");
                continue;
            }
            JsonNode? got = JsonNode.Parse(parsed.ToJson());
            JsonNode? want = JsonNode.Parse(expectParsed.GetRawText());
            if (!JsonNode.DeepEquals(got, want))
            {
                failures.Add($"{name}:\n  got    {got?.ToJsonString()}\n  expect {want?.ToJsonString()}");
            }
        }
        Assert.Equal(17, walked);
        Assert.True(failures.Count == 0,
            $"{failures.Count} tolerance failures:\n" + string.Join("\n", failures));
    }
}
