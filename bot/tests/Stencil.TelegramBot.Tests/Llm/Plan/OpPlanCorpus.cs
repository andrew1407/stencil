using System.Text.Json;

namespace Stencil.TelegramBot.Tests.Llm.Plan;

/// <summary>One op-plan fixture, flattened at load so the walker holds no JsonDocument.</summary>
internal sealed record OpPlanFixture(
    string File,
    bool Generated,
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

/// <summary>One adversarial input of <c>oracle/inputs.json</c>: its reply text and the JS reference's verdict.</summary>
internal sealed record OpPlanOracleCase(string Name, string Expect, string InputText);

/// <summary>The shared op-plan conformance corpus (<c>common/fixtures/llm/opPlan/</c>), read once: the hand-written bundle plus the registry-generated one (<c>browser/tools/genOpPlanFixtures.mjs</c>), keyed by label so <c>[MemberData]</c> carries one short string; the adversarial oracle inputs; and <c>generated/normalized.json</c>, core's result per case under the bot's surface.</summary>
internal static class OpPlanCorpus
{
    private static readonly Lazy<IReadOnlyList<OpPlanOracleCase>> _oracle = new(loadOracle);
    private static readonly Lazy<IReadOnlyDictionary<string, string>> _golden = new(loadGolden);

    public static IReadOnlyList<OpPlanOracleCase> Oracle => _oracle.Value;

    /// <summary>Case name → core's result JSON for surface <c>bot</c>, as the golden records it.</summary>
    public static IReadOnlyDictionary<string, string> Golden => _golden.Value;

    private static readonly Lazy<IReadOnlyList<OpPlanFixture>> _loaded = new(load);
    private static readonly Lazy<IReadOnlyDictionary<string, OpPlanFixture>> _index =
        new(() => _loaded.Value.ToDictionary(f => f.File, StringComparer.Ordinal));

    public static IReadOnlyList<OpPlanFixture> All => _loaded.Value;

    /// <summary>Each bundle's own case count, so a walker can floor them separately.</summary>
    public static int HandCount => All.Count(f => f.Generated is false);

    public static int GeneratedCount => All.Count(f => f.Generated);

    public static OpPlanFixture ByFile(string file) => _index.Value[file];

    public static IEnumerable<string> FileNames => All.Select(f => f.File);

    public static IEnumerable<string> BotFileNames => All.Where(f => f.AppliesToBot).Select(f => f.File);

    private static IReadOnlyList<OpPlanFixture> load()
    {
        string dir = SharedFixtures.LlmFixtureDir("opPlan");
        List<OpPlanFixture> fixtures = [];
        using JsonDocument hand = SharedFixtures.Load(Path.Combine(dir, "cases.json"));
        foreach (JsonElement fx in hand.RootElement.GetProperty("cases").EnumerateArray())
        {
            fixtures.Add(read(fx.GetProperty("file").GetString()!, fx, generated: false));
        }
        using JsonDocument bundle = SharedFixtures.Load(Path.Combine(dir, "generated", "cases.json"));
        foreach (JsonElement fx in bundle.RootElement.GetProperty("cases").EnumerateArray())
        {
            fixtures.Add(read($"{fx.GetProperty("name").GetString()}.json", fx, generated: true));
        }
        return fixtures;
    }

    private static IReadOnlyList<OpPlanOracleCase> loadOracle()
    {
        using JsonDocument doc = SharedFixtures.Load(Path.Combine(SharedFixtures.LlmFixtureDir("opPlan"), "oracle", "inputs.json"));
        return [.. doc.RootElement.GetProperty("cases").EnumerateArray().Select(fx => new OpPlanOracleCase(
            fx.GetProperty("name").GetString()!, fx.GetProperty("expect").GetString()!, TextOf(fx)))];
    }

    private static IReadOnlyDictionary<string, string> loadGolden()
    {
        using JsonDocument doc = SharedFixtures.Load(
            Path.Combine(SharedFixtures.LlmFixtureDir("opPlan"), "generated", "normalized.json"));
        Dictionary<string, string> golden = new(StringComparer.Ordinal);
        foreach (JsonElement c in doc.RootElement.GetProperty("cases").EnumerateArray())
        {
            foreach (JsonElement r in c.GetProperty("results").EnumerateArray())
            {
                if (r.GetProperty("surfaces").EnumerateArray().Any(s => s.GetString() == "bot"))
                {
                    golden[c.GetProperty("name").GetString()!] = r.GetProperty("json").GetString()!;
                }
            }
        }
        return golden;
    }

    /// <summary>A case's reply as a model would send it: verbatim, object-serialised, chunks repeated (<c>parts</c>) or raw bytes decoded as a reply arrives (<c>inputBase64</c>).</summary>
    public static string TextOf(JsonElement fx)
    {
        if (fx.TryGetProperty("parts", out JsonElement parts))
        {
            return string.Concat(parts.EnumerateArray().SelectMany(p => Enumerable.Repeat(p[0].GetString()!, p[1].GetInt32())));
        }
        if (fx.TryGetProperty("inputBase64", out JsonElement b64))
        {
            return System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(b64.GetString()!));
        }
        JsonElement input = fx.GetProperty("input");
        return input.ValueKind == JsonValueKind.String ? input.GetString()! : input.GetRawText();
    }

    private static OpPlanFixture read(string file, JsonElement fx, bool generated)
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
            generated,
            fx.TryGetProperty("name", out JsonElement n) ? n.GetString() ?? "" : "",
            profilesOk,
            names,
            fx.TryGetProperty("expect", out JsonElement e) ? e.GetString() : null,
            fx.TryGetProperty("reason", out JsonElement r) && r.ValueKind == JsonValueKind.String ? r.GetString() : null,
            hasInput,
            hasInput ? TextOf(fx) : "",
            divergences);
    }
}
