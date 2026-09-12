using System.Text.Json;
using System.Text.RegularExpressions;
using Stencil.TelegramBot.Application.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The shared op-plan corpus (<c>browser/js/config/llm/fixtures/opPlan/</c>) against the real
/// <see cref="OpPlanParser"/> — the bot's port of <c>browser/tests/opPlanFixtures.test.js</c>.
/// Profiles <c>bot</c>/<c>all</c> run; verdict = override ?? knownDivergence.bot ?? expect.
/// One test per fixture, with only the file name in <c>[MemberData]</c>.
/// </summary>
public sealed class OpPlanFixtureWalkerTests
{
    private static readonly string[] Profiles = ["editor", "console", "bot", "mcp", "extension", "all"];
    private static readonly string[] Surfaces = ["browser", "desktop", "cli", "pystencil", "bot", "mcp", "extension"];

    public static TheoryData<string> AllFixtures() => SharedFixtures.TheoryNames(OpPlanCorpus.FileNames);

    public static TheoryData<string> BotFixtures() => SharedFixtures.TheoryNames(OpPlanCorpus.BotFileNames);

    [Fact]
    public void TheCorpusIsBigEnoughToBeReal()
    {
        // Floors per bundle, not on the total: the generated cases alone clear a combined
        // floor, so a vanished cases.json would otherwise walk green.
        Assert.True(OpPlanCorpus.HandCount >= 180, $"hand-written cases.json collapsed to {OpPlanCorpus.HandCount}");
        Assert.True(OpPlanCorpus.GeneratedCount >= 400, $"generated/cases.json collapsed to {OpPlanCorpus.GeneratedCount}");
        int bot = OpPlanCorpus.BotFileNames.Count();
        Assert.True(bot > 100, $"suspiciously few bot fixtures ({bot})");
    }

    [Theory]
    [MemberData(nameof(AllFixtures))]
    public void FixtureIsWellFormed(string file)
    {
        // Port of the reference walker's corpus-shape check.
        OpPlanFixture fx = OpPlanCorpus.ByFile(file);
        List<string> problems = new();
        if ($"{fx.Name}.json" != Regex.Replace(file, @"^\d+-", ""))
        {
            problems.Add("\"name\" must match the filename slug");
        }
        if (!fx.ProfilesWellFormed)
        {
            problems.Add("\"profiles\" must be a non-empty array");
        }
        foreach (string? profile in fx.Profiles.Where(p => !Profiles.Contains(p)))
        {
            problems.Add($"unknown profile \"{profile}\"");
        }
        if (fx.Expect is not ("valid" or "invalid"))
        {
            problems.Add("\"expect\" must be valid|invalid");
        }
        if (!fx.HasInput)
        {
            problems.Add("\"input\" is required");
        }
        if (fx.Expect == "invalid" && string.IsNullOrEmpty(fx.Reason))
        {
            problems.Add("invalid cases need a \"reason\"");
        }
        foreach ((string surface, string? verdict) in fx.KnownDivergence)
        {
            if (!Surfaces.Contains(surface))
            {
                problems.Add($"unknown knownDivergence surface \"{surface}\"");
            }
            if (verdict is not ("valid" or "invalid"))
            {
                problems.Add("knownDivergence verdicts are valid|invalid");
            }
        }
        Assert.True(problems.Count == 0, $"{file}:\n  " + string.Join("\n  ", problems));
    }

    [Theory]
    [MemberData(nameof(BotFixtures))]
    public void BotFixtureGetsItsVerdict(string file)
    {
        OpPlanFixture fx = OpPlanCorpus.ByFile(file);
        // Verdict precedence: local override ?? knownDivergence.bot ?? expect.
        string want = SharedFixtures.OverrideFor("opPlan", fx.Name) is JsonElement ov
            ? ov.GetProperty("verdict").GetString()!
            : fx.KnownDivergence.FirstOrDefault(d => d.Surface == "bot").Verdict ?? fx.Expect!;

        OpPlanParseResult result = OpPlanParser.Parse(fx.InputText);
        bool valid = result.Error is null && result.Plan is not null;

        Assert.True(valid == (want == "valid"),
            $"{file}: expected {want}, got " + (valid ? "a plan" : $"error \"{result.Error}\""));
    }
}
