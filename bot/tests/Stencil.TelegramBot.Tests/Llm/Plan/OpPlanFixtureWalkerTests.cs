using System.Text.Json;
using System.Text.RegularExpressions;
using Stencil.TelegramBot.Application.Llm.Plan;

namespace Stencil.TelegramBot.Tests.Llm.Plan;

/// <summary>The shared op-plan corpus through the bot's typed mapper, fed core's own results from <c>generated/normalized.json</c> — the bot's port of <c>browser/tests/opPlanFixtures.test.js</c>. Profiles <c>bot</c>/<c>all</c> run; verdict = override ?? knownDivergence.bot ?? expect.</summary>
public sealed class OpPlanFixtureWalkerTests
{
    private static readonly string[] _profiles = ["editor", "console", "bot", "mcp", "extension", "all"];
    private static readonly string[] _surfaces = ["browser", "desktop", "cli", "pystencil", "bot", "mcp", "extension"];

    public static TheoryData<string> AllFixtures() => SharedFixtures.TheoryNames(OpPlanCorpus.FileNames);

    public static TheoryData<string> BotFixtures() => SharedFixtures.TheoryNames(OpPlanCorpus.BotFileNames);

    [Fact]
    public void Should_Have_A_Corpus_Big_Enough_To_Be_Real()
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
    public void Should_Be_Well_Formed_For_Each_Fixture(string file)
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
        foreach (string? profile in fx.Profiles.Where(p => !_profiles.Contains(p)))
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
            if (!_surfaces.Contains(surface))
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
    public void Should_Give_Each_Bot_Fixture_Its_Verdict(string file)
    {
        OpPlanFixture fx = OpPlanCorpus.ByFile(file);
        // Verdict precedence: local override ?? knownDivergence.bot ?? expect.
        string want = SharedFixtures.OverrideFor("opPlan", fx.Name) is JsonElement ov
            ? ov.GetProperty("verdict").GetString()!
            : fx.KnownDivergence.FirstOrDefault(d => d.Surface == "bot").Verdict ?? fx.Expect!;

        assertVerdict(fx.Name, want);
    }

    /// <summary>The adversarial inputs: core's verdict, through the typed mapper, is the reference's.</summary>
    [Theory]
    [MemberData(nameof(OracleCases))]
    public void Should_Give_Each_Oracle_Case_Its_Verdict(string name) =>
        assertVerdict(name, OpPlanCorpus.Oracle.Single(c => c.Name == name).Expect);

    public static TheoryData<string> OracleCases() => SharedFixtures.TheoryNames(OpPlanCorpus.Oracle.Select(c => c.Name));

    // Offline: core's recorded result for the bot's surface, never a CLI spawn.
    private static void assertVerdict(string name, string want)
    {
        Assert.True(OpPlanCorpus.Golden.TryGetValue(name, out string? result), $"{name}: no bot result in normalized.json");
        OpPlanParseResult mapped = OpPlanParser.Map(result);
        bool valid = mapped.Error is null && mapped.Plan is not null;

        Assert.True(valid == (want == "valid"),
            $"{name}: expected {want}, got " + (valid ? "a plan" : $"error \"{mapped.Error}\""));
    }
}
