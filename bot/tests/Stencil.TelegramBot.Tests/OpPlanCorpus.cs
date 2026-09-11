using System.Text.Json;

namespace Stencil.TelegramBot.Tests;

/// <summary>One op-plan fixture, flattened at load so the walker holds no JsonDocument.</summary>
internal sealed record OpPlanFixture(
    string File,
    string Name,
    bool ProfilesWellFormed,
    IReadOnlyList<string?> Profiles,
    string? Expect,
    string? Reason,
    bool HasInput,
    string InputText,
    IReadOnlyList<(string Surface, string? Verdict)> KnownDivergence)
{
    public bool AppliesToBot => Profiles.Any(p => p is "bot" or "all");
}

/// <summary>
/// The shared op-plan conformance corpus (<c>browser/js/config/llm/fixtures/opPlan/</c>),
/// read once: the hand-written files plus the registry-generated bundle
/// (<c>browser/tools/genOpPlanFixtures.mjs</c>), each bundle case a pseudo-file. Keyed by
/// file name so <c>[MemberData]</c> carries one short string per test.
/// </summary>
internal static class OpPlanCorpus
{
    private static readonly Lazy<IReadOnlyList<OpPlanFixture>> Loaded = new(Load);
    private static readonly Lazy<IReadOnlyDictionary<string, OpPlanFixture>> Index =
        new(() => Loaded.Value.ToDictionary(f => f.File, StringComparer.Ordinal));

    public static IReadOnlyList<OpPlanFixture> All => Loaded.Value;

    public static OpPlanFixture ByFile(string file) => Index.Value[file];

    public static IEnumerable<string> FileNames => All.Select(f => f.File);

    public static IEnumerable<string> BotFileNames => All.Where(f => f.AppliesToBot).Select(f => f.File);

    private static IReadOnlyList<OpPlanFixture> Load()
    {
        string dir = SharedFixtures.LlmFixtureDir("opPlan");
        List<OpPlanFixture> fixtures = [];
        foreach (string path in Directory.GetFiles(dir, "*.json").OrderBy(static p => p, StringComparer.Ordinal))
        {
            using JsonDocument doc = SharedFixtures.Load(path);
            fixtures.Add(Read(Path.GetFileName(path), doc.RootElement));
        }
        using JsonDocument bundle = SharedFixtures.Load(Path.Combine(dir, "generated", "cases.json"));
        foreach (JsonElement fx in bundle.RootElement.GetProperty("cases").EnumerateArray())
        {
            fixtures.Add(Read($"{fx.GetProperty("name").GetString()}.json", fx));
        }
        return fixtures;
    }

    private static OpPlanFixture Read(string file, JsonElement fx)
    {
        bool profilesOk = fx.TryGetProperty("profiles", out JsonElement profiles)
            && profiles.ValueKind == JsonValueKind.Array && profiles.GetArrayLength() > 0;
        List<string?> names = profilesOk
            ? [.. profiles.EnumerateArray().Select(p => p.ValueKind == JsonValueKind.String ? p.GetString() : null)]
            : [];
        List<(string, string?)> divergences = [];
        if (fx.TryGetProperty("knownDivergence", out JsonElement kd) && kd.ValueKind == JsonValueKind.Object)
        {
            foreach (JsonProperty prop in kd.EnumerateObject())
            {
                divergences.Add((prop.Name, prop.Value.ValueKind == JsonValueKind.String ? prop.Value.GetString() : null));
            }
        }
        bool hasInput = fx.TryGetProperty("input", out JsonElement input) && input.ValueKind != JsonValueKind.Null;
        return new OpPlanFixture(
            file,
            fx.TryGetProperty("name", out JsonElement n) ? n.GetString() ?? "" : "",
            profilesOk,
            names,
            fx.TryGetProperty("expect", out JsonElement e) ? e.GetString() : null,
            fx.TryGetProperty("reason", out JsonElement r) && r.ValueKind == JsonValueKind.String ? r.GetString() : null,
            hasInput,
            !hasInput ? "" : input.ValueKind == JsonValueKind.String ? input.GetString()! : input.GetRawText(),
            divergences);
    }
}
