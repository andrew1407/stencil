using System.Text.Json;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Locates the shared, language-neutral fixture corpus under <c>browser/js/config/</c> by
/// walking up from the test binary's directory to the repo root (marker: the <c>browser/</c>
/// front-end), and loads this suite's local <c>FixtureOverrides.json</c> — the pinned list of
/// cases where the bot's measured behavior diverges from the corpus expectation.
/// </summary>
internal static class SharedFixtures
{
    private static readonly Lazy<string> Root = new(FindRepoRoot);

    public static string RepoRoot => Root.Value;

    private static string FindRepoRoot()
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

    private static readonly Lazy<JsonDocument> Overrides = new(() =>
        JsonDocument.Parse(File.ReadAllText(PathOf(
            "bot", "tests", "Stencil.TelegramBot.Tests", "FixtureOverrides.json"))));

    /// <summary>
    /// The local override for a fixture, or null when the bot agrees with the corpus. Keyed
    /// family → case name; the entry's fields are family-specific (verdict / message / kind /
    /// expectParsed) plus a mandatory human note.
    /// </summary>
    public static JsonElement? OverrideFor(string family, string name)
    {
        if (Overrides.Value.RootElement.TryGetProperty(family, out JsonElement section)
            && section.TryGetProperty(name, out JsonElement entry))
        {
            return entry;
        }
        return null;
    }
}
