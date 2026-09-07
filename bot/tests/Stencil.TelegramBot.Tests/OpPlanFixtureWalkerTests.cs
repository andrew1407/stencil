using System.Text.Json;
using Stencil.TelegramBot.Application.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Walks the shared op-plan conformance corpus
/// (<c>browser/js/config/llm/fixtures/opPlan/</c>, see <c>_schema.md</c>) against the REAL
/// <see cref="OpPlanParser"/> — the bot's port of the reference walker
/// <c>browser/tests/opPlanFixtures.test.js</c>. Fixtures whose profiles include
/// <c>bot</c>/<c>all</c> run; the verdict is the local <c>FixtureOverrides.json</c> entry,
/// else <c>knownDivergence.bot</c>, else <c>expect</c>. Valid = <see cref="OpPlanParser.Parse"/>
/// returns a plan (chat-only counts); invalid = it returns a plan-level error.
/// </summary>
public sealed class OpPlanFixtureWalkerTests
{
    private static readonly string[] Profiles = ["editor", "console", "bot", "mcp", "extension", "all"];
    private static readonly string[] Surfaces = ["browser", "desktop", "cli", "pystencil", "bot", "mcp", "extension"];
    private static readonly string[] BotProfiles = ["bot", "all"];

    private static IEnumerable<(string File, JsonDocument Doc)> Fixtures()
    {
        string dir = SharedFixtures.LlmFixtureDir("opPlan");
        foreach (string path in Directory.GetFiles(dir, "*.json").OrderBy(static p => p, StringComparer.Ordinal))
        {
            yield return (Path.GetFileName(path), SharedFixtures.Load(path));
        }
        // The registry-generated bundle (browser/tools/genOpPlanFixtures.mjs): one pseudo-file per case.
        using JsonDocument bundle = SharedFixtures.Load(Path.Combine(dir, "generated", "cases.json"));
        foreach (JsonElement fx in bundle.RootElement.GetProperty("cases").EnumerateArray())
        {
            yield return ($"{fx.GetProperty("name").GetString()}.json", JsonDocument.Parse(fx.GetRawText()));
        }
    }

    [Fact]
    public void TheCorpusExistsAndIsWellFormed()
    {
        // Port of the reference walker's corpus-shape check.
        List<string> problems = new();
        int count = 0;
        foreach ((string file, JsonDocument doc) in Fixtures())
        {
            using (doc)
            {
                count++;
                JsonElement fx = doc.RootElement;
                string name = fx.TryGetProperty("name", out JsonElement n) ? n.GetString() ?? "" : "";
                if ($"{name}.json" != System.Text.RegularExpressions.Regex.Replace(file, @"^\d+-", ""))
                {
                    problems.Add($"{file}: \"name\" must match the filename slug");
                }
                if (!fx.TryGetProperty("profiles", out JsonElement profiles)
                    || profiles.ValueKind != JsonValueKind.Array || profiles.GetArrayLength() == 0)
                {
                    problems.Add($"{file}: \"profiles\" must be a non-empty array");
                }
                else
                {
                    foreach (JsonElement p in profiles.EnumerateArray())
                    {
                        if (!Profiles.Contains(p.GetString()))
                        {
                            problems.Add($"{file}: unknown profile \"{p}\"");
                        }
                    }
                }
                string? expect = fx.TryGetProperty("expect", out JsonElement e) ? e.GetString() : null;
                if (expect is not ("valid" or "invalid"))
                {
                    problems.Add($"{file}: \"expect\" must be valid|invalid");
                }
                if (!fx.TryGetProperty("input", out JsonElement input) || input.ValueKind == JsonValueKind.Null)
                {
                    problems.Add($"{file}: \"input\" is required");
                }
                if (expect == "invalid"
                    && (!fx.TryGetProperty("reason", out JsonElement r)
                        || r.ValueKind != JsonValueKind.String || r.GetString()!.Length == 0))
                {
                    problems.Add($"{file}: invalid cases need a \"reason\"");
                }
                if (fx.TryGetProperty("knownDivergence", out JsonElement kd))
                {
                    foreach (JsonProperty prop in kd.EnumerateObject())
                    {
                        if (!Surfaces.Contains(prop.Name))
                        {
                            problems.Add($"{file}: unknown knownDivergence surface \"{prop.Name}\"");
                        }
                        if (prop.Value.GetString() is not ("valid" or "invalid"))
                        {
                            problems.Add($"{file}: knownDivergence verdicts are valid|invalid");
                        }
                    }
                }
            }
        }
        Assert.True(count >= 80, $"expected a real corpus, found {count} fixtures");
        Assert.True(problems.Count == 0, string.Join("\n", problems));
    }

    [Fact]
    public void EveryBotFixtureGetsItsVerdict()
    {
        List<string> failures = new();
        int walked = 0, skipped = 0;
        foreach ((string file, JsonDocument doc) in Fixtures())
        {
            using (doc)
            {
                JsonElement fx = doc.RootElement;
                bool applies = fx.GetProperty("profiles").EnumerateArray()
                    .Any(static p => BotProfiles.Contains(p.GetString()));
                if (!applies)
                {
                    skipped++;
                    continue;
                }
                walked++;
                string name = fx.GetProperty("name").GetString()!;
                // Verdict precedence: local override ?? knownDivergence.bot ?? expect.
                string want = SharedFixtures.OverrideFor("opPlan", name) is JsonElement ov
                    ? ov.GetProperty("verdict").GetString()!
                    : fx.TryGetProperty("knownDivergence", out JsonElement kd)
                        && kd.TryGetProperty("bot", out JsonElement botKd)
                        ? botKd.GetString()!
                        : fx.GetProperty("expect").GetString()!;

                JsonElement input = fx.GetProperty("input");
                string text = input.ValueKind == JsonValueKind.String
                    ? input.GetString()!
                    : input.GetRawText();

                OpPlanParseResult result = OpPlanParser.Parse(text);
                bool valid = result.Error is null && result.Plan is not null;
                if (valid != (want == "valid"))
                {
                    failures.Add($"{file}: expected {want}, got "
                        + (valid ? "a plan" : $"error \"{result.Error}\""));
                }
            }
        }
        Assert.True(walked > 100, $"suspiciously few bot fixtures walked ({walked})");
        Assert.True(failures.Count == 0,
            $"{failures.Count} verdict mismatches (walked {walked}, skipped {skipped}):\n" + string.Join("\n", failures));
    }
}
