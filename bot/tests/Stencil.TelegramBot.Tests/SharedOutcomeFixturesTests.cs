using System.Runtime.CompilerServices;
using System.Text.Json;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Replay the shared, language-neutral golden fixtures for the CLI stderr OUTPUT grammar
/// through <see cref="CliOutcomeParser"/>. The <b>same</b> file
/// (<c>cli/testdata/outcome_fixtures.json</c>) is replayed by the Rust MCP server's
/// <c>fixtures_test.rs</c>, so if the two parsers ever disagree on a case, one of the suites
/// goes red — that is the drift this catches. Per-parser unit cases still live in
/// <see cref="CliOutcomeParserTests"/>; this asserts conformance to the canonical contract
/// (<c>cli/CONTRACT.md</c>). Uses only <c>System.Text.Json</c> from the BCL — no new deps.
/// </summary>
public sealed class SharedOutcomeFixturesTests
{
    /// <summary>Locate the shared fixture file relative to THIS test source (compile-time
    /// path), so resolution is independent of the test's working directory.</summary>
    private static string FixturesPath([CallerFilePath] string thisFile = "")
    {
        string dir = Path.GetDirectoryName(thisFile)!;
        // .../bot/tests/Stencil.TelegramBot.Tests -> repo root is three levels up.
        return Path.GetFullPath(
            Path.Combine(dir, "..", "..", "..", "cli", "testdata", "outcome_fixtures.json"));
    }

    private static JsonElement Section(string name)
    {
        string json = File.ReadAllText(FixturesPath());
        using JsonDocument doc = JsonDocument.Parse(json);
        return doc.RootElement.GetProperty(name).Clone();
    }

    private static string Stderr(JsonElement c) => c.GetProperty("stderr").GetString()!;

    private static string Name(JsonElement c) =>
        c.TryGetProperty("name", out JsonElement n) ? n.GetString() ?? "<unnamed>" : "<unnamed>";

    [Fact]
    public void WroteFixturesMatch()
    {
        foreach (JsonElement c in Section("wrote").EnumerateArray())
        {
            RenderResult? got = CliOutcomeParser.ParseWrote(Stderr(c));
            JsonElement expected = c.GetProperty("expected");
            if (expected.ValueKind == JsonValueKind.Null)
            {
                Assert.True(got is null, $"[{Name(c)}] expected no success line, got {got}");
            }
            else
            {
                Assert.NotNull(got);
                Assert.Equal(expected.GetProperty("path").GetString(), got!.Path);
                Assert.Equal(expected.GetProperty("width").GetInt32(), got.Width);
                Assert.Equal(expected.GetProperty("height").GetInt32(), got.Height);
            }
        }
    }

    [Fact]
    public void RemoteFixturesMatch()
    {
        foreach (JsonElement c in Section("remotes").EnumerateArray())
        {
            IReadOnlyList<RemoteDelivery> got = CliOutcomeParser.ParseRemotes(Stderr(c));
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
                        Assert.Fail($"[{Name(c)}] unexpected delivery type {got[i].GetType().Name}");
                        break;
                }
                i++;
            }
        }
    }

    [Fact]
    public void ErrorFixturesMatch()
    {
        foreach (JsonElement c in Section("errors").EnumerateArray())
        {
            string got = CliOutcomeParser.ExtractErrors(Stderr(c));
            Assert.Equal(c.GetProperty("expected").GetString(), got);
        }
    }
}
