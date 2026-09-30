using System.Text.Json;
using Stencil.TelegramBot.Application.Llm;

namespace Stencil.TelegramBot.Tests.Llm;

/// <summary>Walks <c>common/fixtures/imageHeader/cases.json</c> through <c>ImageDimensionReader.TryRead</c>, which reports a size and no format. A case the bot measures differently on purpose is pinned in <c>FixtureOverrides.json</c> under <c>imageHeader</c>.</summary>
public sealed class ImageHeaderCorpusTests
{
    private static readonly string _path = Path.Combine(SharedFixtures.ConfigFixtureDir("imageHeader"), "cases.json");

    public static TheoryData<string> Names => SharedFixtures.TheoryNames(SharedFixtures.CaseNames(_path));

    [Theory]
    [MemberData(nameof(Names))]
    public void Should_Measure_Each_Header_As_The_Corpus_Expects(string name)
    {
        using JsonDocument doc = SharedFixtures.Case(_path, name);
        JsonElement expect = doc.RootElement.GetProperty("expect");
        if (SharedFixtures.OverrideFor("imageHeader", name) is JsonElement pinned)
        {
            expect = pinned.GetProperty("expect");
        }
        byte[] bytes = Convert.FromBase64String(doc.RootElement.GetProperty("base64").GetString()!);

        bool measured = ImageDimensionReader.TryRead(bytes, out int width, out int height);

        Assert.Equal(expect.ValueKind != JsonValueKind.Null, measured);
        if (measured)
        {
            Assert.Equal(expect.GetProperty("width").GetInt64(), width);
            Assert.Equal(expect.GetProperty("height").GetInt64(), height);
        }
    }
}
