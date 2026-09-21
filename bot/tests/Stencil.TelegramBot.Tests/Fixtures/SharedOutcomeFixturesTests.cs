using System.Runtime.CompilerServices;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests.Fixtures;

/// <summary>The shared, language-neutral goldens for the CLI stderr grammar (<c>cli/testdata/outcome_fixtures.json</c>) through <see cref="CliOutcomeParser"/>. The same file is replayed by the Rust MCP server's <c>fixtures_test.rs</c>, so a disagreement between the two parsers reddens one of the suites.</summary>
public sealed class SharedOutcomeFixturesTests
{
    /// <summary>Locate the shared fixture file relative to THIS test source (compile-time
    /// path), so resolution is independent of the test's working directory.</summary>
    private static string fixturesPath([CallerFilePath] string thisFile = "")
    {
        string dir = Path.GetDirectoryName(thisFile)!;
        // .../bot/tests/Stencil.TelegramBot.Tests -> repo root is three levels up.
        return Path.GetFullPath(
            Path.Combine(dir, "..", "..", "..", "..", "cli", "testdata", "outcome_fixtures.json"));
    }

    private static readonly Lazy<JsonDocument> _corpus =
        new(() => JsonDocument.Parse(File.ReadAllText(fixturesPath())));

    private static IEnumerable<JsonElement> sectionOf(string name) =>
        _corpus.Value.RootElement.GetProperty(name).EnumerateArray();

    private static string nameOf(JsonElement c) =>
        c.TryGetProperty("name", out JsonElement n) ? n.GetString() ?? "<unnamed>" : "<unnamed>";

    private static JsonElement caseOf(string section, string name) =>
        sectionOf(section).First(c => nameOf(c) == name);

    private static TheoryData<string> names(string section)
    {
        TheoryData<string> data = new();
        foreach (JsonElement c in sectionOf(section))
        {
            data.Add(nameOf(c));
        }
        Assert.True(data.Count > 0, $"the \"{section}\" section of the corpus is empty");
        return data;
    }

    private static string stderr(JsonElement c) => c.GetProperty("stderr").GetString()!;

    public static TheoryData<string> WroteCases() => names("wrote");

    public static TheoryData<string> RemoteCases() => names("remotes");

    public static TheoryData<string> ErrorCases() => names("errors");

    [Theory]
    [MemberData(nameof(WroteCases))]
    public void Should_Match_Each_Wrote_Fixture(string name)
    {
        JsonElement c = caseOf("wrote", name);
        RenderResult? got = CliOutcomeParser.ParseWrote(stderr(c));
        JsonElement expected = c.GetProperty("expected");
        if (expected.ValueKind == JsonValueKind.Null)
        {
            Assert.True(got is null, $"expected no success line, got {got}");
            return;
        }
        Assert.NotNull(got);
        Assert.Equal(expected.GetProperty("path").GetString(), got!.Path);
        Assert.Equal(expected.GetProperty("width").GetInt32(), got.Width);
        Assert.Equal(expected.GetProperty("height").GetInt32(), got.Height);
    }

    [Theory]
    [MemberData(nameof(RemoteCases))]
    public void Should_Match_Each_Remote_Fixture(string name)
    {
        JsonElement c = caseOf("remotes", name);
        IReadOnlyList<RemoteDelivery> got = CliOutcomeParser.ParseRemotes(stderr(c));
        JsonElement expected = c.GetProperty("expected");
        Assert.Equal(expected.GetArrayLength(), got.Count);

        int i = 0;
        foreach (JsonElement e in expected.EnumerateArray())
        {
            string action = e.GetProperty("action").GetString()!;
            switch (got[i])
            {
                case RemoteDelivery.Updated updated:
                    Assert.Equal("updated", action);
                    Assert.Equal(e.GetProperty("id").GetString(), updated.Id);
                    Assert.Equal(e.GetProperty("width").GetInt32(), updated.Width);
                    Assert.Equal(e.GetProperty("height").GetInt32(), updated.Height);
                    break;
                case RemoteDelivery.Created created:
                    Assert.Equal("created", action);
                    Assert.Equal(e.GetProperty("name").GetString(), created.Name);
                    Assert.Equal(e.GetProperty("id").GetString(), created.Id);
                    break;
                default:
                    Assert.Fail($"unexpected delivery type {got[i].GetType().Name}");
                    break;
            }
            i++;
        }
    }

    [Theory]
    [MemberData(nameof(ErrorCases))]
    public void Should_Match_Each_Error_Fixture(string name)
    {
        JsonElement c = caseOf("errors", name);
        Assert.Equal(c.GetProperty("expected").GetString(), CliOutcomeParser.ExtractErrors(stderr(c)));
    }
}
