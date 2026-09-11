using System.Text.Json;
using System.Text.Json.Nodes;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The shared §12.1 chat-document vectors through <see cref="ChatDocument"/>, one test per
/// vector: roundtrip.json pins parse→serialize→parse as the identity, tolerance.json the
/// lenient reads. Object-form cases are serialized first (the bot parses strings only); two
/// measured divergences are pinned in <c>FixtureOverrides.json</c>.
/// </summary>
public sealed class ChatDocFixtureWalkerTests
{
    private static string Roundtrip => Path.Combine(SharedFixtures.LlmFixtureDir("chatDoc"), "roundtrip.json");
    private static string Tolerance => Path.Combine(SharedFixtures.LlmFixtureDir("chatDoc"), "tolerance.json");

    public static TheoryData<string> RoundtripDocs() => SharedFixtures.TheoryNames(SharedFixtures.CaseNames(Roundtrip));

    public static TheoryData<string> ToleranceDocs() => SharedFixtures.TheoryNames(SharedFixtures.CaseNames(Tolerance));

    [Fact]
    public void BothCorporaHaveEveryVector()
    {
        Assert.Equal(5, SharedFixtures.Cases(Roundtrip).Count);
        Assert.Equal(17, SharedFixtures.Cases(Tolerance).Count);
    }

    [Theory]
    [MemberData(nameof(RoundtripDocs))]
    public void RoundtripDocIsAFixedPoint(string name)
    {
        using JsonDocument doc = SharedFixtures.Case(Roundtrip, name);
        string raw = doc.RootElement.GetProperty("doc").GetRawText();

        ChatDocument? parsed = ChatDocument.TryParse(raw);
        Assert.True(parsed is not null, "TryParse returned null for a canonical document");

        JsonNode? expected = JsonNode.Parse(raw);
        JsonNode? reserialized = JsonNode.Parse(parsed!.ToJson());
        Assert.True(JsonNode.DeepEquals(reserialized, expected),
            $"serialize∘parse is not the identity\n  got    {reserialized?.ToJsonString()}\n  expect {expected?.ToJsonString()}");

        // …and parsing the re-serialized form converges (a true fixed point).
        ChatDocument? again = ChatDocument.TryParse(parsed.ToJson());
        Assert.True(again is not null && JsonNode.DeepEquals(JsonNode.Parse(again.ToJson()), expected),
            "parse(serialize(parse(doc))) diverged");
    }

    [Theory]
    [MemberData(nameof(ToleranceDocs))]
    public void TolerantReadMatchesItsPin(string name)
    {
        using JsonDocument doc = SharedFixtures.Case(Tolerance, name);
        JsonElement fx = doc.RootElement;
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
            Assert.True(parsed is null, $"expected null (missing document), got {parsed?.ToJson()}");
            return;
        }
        Assert.True(parsed is not null, "expected a document, got null");

        JsonNode? got = JsonNode.Parse(parsed!.ToJson());
        JsonNode? want = JsonNode.Parse(expectParsed.GetRawText());
        Assert.True(JsonNode.DeepEquals(got, want),
            $"\n  got    {got?.ToJsonString()}\n  expect {want?.ToJsonString()}");
    }
}
