using System.Collections.Concurrent;
using System.Text.Json;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Locates the shared fixture corpus under <c>browser/js/config/</c> (walking up to the repo
/// root) and this suite's <c>FixtureOverrides.json</c> — the pinned cases where the bot's
/// measured behavior diverges from the corpus expectation.
/// </summary>
internal static class SharedFixtures
{
    private static readonly Lazy<string> _root = new(findRepoRoot);

    public static string RepoRoot => _root.Value;

    private static string findRepoRoot()
    {
        for (string? dir = AppContext.BaseDirectory; dir is not null; dir = Path.GetDirectoryName(dir))
        {
            if (Directory.Exists(Path.Combine(dir, "browser", "js", "config")))
            {
                return dir;
            }
        }
        throw new InvalidOperationException(
            $"could not find the repo root (a directory containing browser/js/config) above {AppContext.BaseDirectory}");
    }

    /// <summary>Absolute path of a file under the repo root.</summary>
    public static string PathOf(params string[] parts) =>
        Path.Combine([RepoRoot, .. parts]);

    /// <summary>The shared LLM fixture directory <c>browser/js/config/llm/fixtures/{name}</c>.</summary>
    public static string LlmFixtureDir(string name) =>
        PathOf("browser", "js", "config", "llm", "fixtures", name);

    /// <summary>The shared config fixture directory <c>browser/js/config/fixtures/{name}</c>.</summary>
    public static string ConfigFixtureDir(string name) =>
        PathOf("browser", "js", "config", "fixtures", name);

    /// <summary>Parse one JSON fixture file into a document (caller disposes).</summary>
    public static JsonDocument Load(string path) =>
        JsonDocument.Parse(File.ReadAllText(path));

    private static readonly ConcurrentDictionary<string, IReadOnlyList<FixtureCase>> _caseCache = new();

    /// <summary>One element of a JSON-array fixture file: its "name" and its raw JSON text.</summary>
    internal sealed record FixtureCase(string Name, string Json)
    {
        public JsonDocument Parse() => JsonDocument.Parse(Json);
    }

    /// <summary>
    /// Every case in a JSON-array fixture file, read once and cached. Walkers pass only the
    /// name through <c>[MemberData]</c>, so discovery never serializes a fixture.
    /// </summary>
    public static IEnumerable<string> CaseNames(string path) =>
        Cases(path).Select(c => c.Name);

    internal static IReadOnlyList<FixtureCase> Cases(string path) =>
        _caseCache.GetOrAdd(path, static p =>
        {
            using JsonDocument doc = Load(p);
            return [.. doc.RootElement.EnumerateArray()
                .Select(e => new FixtureCase(e.GetProperty("name").GetString()!, e.GetRawText()))];
        });

    /// <summary>Wrap names as xUnit theory rows — one discovered test per fixture case.</summary>
    public static TheoryData<string> TheoryNames(IEnumerable<string> names)
    {
        TheoryData<string> data = new();
        foreach (string name in names)
        {
            data.Add(name);
        }
        return data;
    }

    /// <summary>The named case of a JSON-array fixture file (caller disposes the document).</summary>
    internal static JsonDocument Case(string path, string name) =>
        Cases(path).First(c => c.Name == name).Parse();

    private static readonly Lazy<JsonDocument> _overrides = new(() =>
        JsonDocument.Parse(File.ReadAllText(PathOf(
            "bot", "tests", "Stencil.TelegramBot.Tests", "FixtureOverrides.json"))));

    /// <summary>
    /// The local override for a fixture, or null when the bot agrees with the corpus. Keyed
    /// family → case name; fields are family-specific, plus a mandatory human note.
    /// </summary>
    public static JsonElement? OverrideFor(string family, string name)
    {
        if (_overrides.Value.RootElement.TryGetProperty(family, out JsonElement section)
            && section.TryGetProperty(name, out JsonElement entry))
        {
            return entry;
        }
        return null;
    }
}
