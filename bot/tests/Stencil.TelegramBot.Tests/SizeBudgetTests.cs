using System.Text.Json;
using Xunit.Abstractions;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The bot's size + comment ratchet, budgeted by <c>SizeBudget.json</c>: no new oversized file,
/// no listed file grows, no directory gets comment-heavier. Paths are repo-relative.
/// </summary>
public sealed class SizeBudgetTests
{
    private readonly ITestOutputHelper _output;

    public SizeBudgetTests(ITestOutputHelper output) => _output = output;

    private readonly record struct Measured(string Path, int Lines, int CommentLines);

    private static readonly Lazy<Measured[]> Sources = new(MeasureAll);

    private static readonly Lazy<JsonDocument> Budget = new(() =>
        JsonDocument.Parse(File.ReadAllText(SharedFixtures.PathOf(
            "bot", "tests", "Stencil.TelegramBot.Tests", "SizeBudget.json"))));

    private static JsonElement Root => Budget.Value.RootElement;

    private static int MaxNewFileLines => Root.GetProperty("maxNewFileLines").GetInt32();

    private static bool IsException(string path) =>
        Root.GetProperty("exceptions").TryGetProperty(path, out _);

    [Fact]
    public void EveryOversizedFileIsListedInTheBudget()
    {
        JsonElement files = Root.GetProperty("files");
        string[] unlisted = Sources.Value
            .Where(f => f.Lines > MaxNewFileLines && !IsException(f.Path))
            .Where(f => !files.TryGetProperty(f.Path, out _))
            .Select(f => $"{f.Path} ({f.Lines} lines)")
            .ToArray();
        Assert.True(unlisted.Length == 0,
            $"new file(s) over {MaxNewFileLines} lines — split them, or add them to SizeBudget.json:\n  "
            + string.Join("\n  ", unlisted));
    }

    [Fact]
    public void ListedFilesDoNotGrow()
    {
        JsonElement files = Root.GetProperty("files");
        List<string> grew = [];
        foreach (Measured file in Sources.Value)
        {
            if (IsException(file.Path) || !files.TryGetProperty(file.Path, out JsonElement budget))
            {
                continue;
            }
            int recorded = budget.GetInt32();
            if (file.Lines > recorded)
            {
                grew.Add($"{file.Path}: {file.Lines} lines > budgeted {recorded}");
            }
            else if (file.Lines * 10 < recorded * 9)
            {
                _output.WriteLine($"ratchet down {file.Path}: {recorded} -> {file.Lines}");
            }
        }
        Assert.True(grew.Count == 0, "budgeted file(s) grew:\n  " + string.Join("\n  ", grew));
    }

    [Fact]
    public void UnlistedFilesStayUnderTheCap()
    {
        JsonElement files = Root.GetProperty("files");
        string[] over = Sources.Value
            .Where(f => !IsException(f.Path) && !files.TryGetProperty(f.Path, out _))
            .Where(f => f.Lines > MaxNewFileLines)
            .Select(f => $"{f.Path} ({f.Lines} lines)")
            .ToArray();
        Assert.True(over.Length == 0,
            $"file(s) over the {MaxNewFileLines}-line cap:\n  " + string.Join("\n  ", over));
    }

    [Fact]
    public void CommentShareDoesNotRise()
    {
        List<string> risen = [];
        foreach (JsonProperty dir in Root.GetProperty("commentPct").EnumerateObject())
        {
            Measured[] inDir = Sources.Value.Where(f => DirOf(f.Path) == dir.Name).ToArray();
            if (inDir.Length == 0)
            {
                continue;
            }
            int total = inDir.Sum(f => f.Lines);
            int comments = inDir.Sum(f => f.CommentLines);
            int pct = total == 0 ? 0 : comments * 100 / total;
            if (pct > dir.Value.GetInt32())
            {
                risen.Add($"{dir.Name}: {pct}% > budgeted {dir.Value.GetInt32()}%");
            }
        }
        Assert.True(risen.Count == 0, "comment share rose:\n  " + string.Join("\n  ", risen));
    }

    private static string DirOf(string path) => path[..path.LastIndexOf('/')];

    private static Measured[] MeasureAll()
    {
        List<Measured> measured = [];
        foreach (string scope in (string[])["src", "tests"])
        {
            string root = SharedFixtures.PathOf("bot", scope);
            foreach (string file in Directory.EnumerateFiles(root, "*.cs", SearchOption.AllDirectories))
            {
                string rel = Path.GetRelativePath(SharedFixtures.RepoRoot, file).Replace('\\', '/');
                if (rel.Contains("/obj/") || rel.Contains("/bin/"))
                {
                    continue;
                }
                string[] lines = File.ReadAllLines(file);
                measured.Add(new Measured(rel, lines.Length, CountCommentLines(lines)));
            }
        }
        return [.. measured.OrderBy(m => m.Path, StringComparer.Ordinal)];
    }

    /// <summary>
    /// Lines opening with <c>//</c> or <c>/*</c>, plus lines inside a block comment. String,
    /// verbatim, raw-string and char literals are tracked so a <c>//</c> inside one never counts.
    /// </summary>
    private static int CountCommentLines(string[] lines)
    {
        bool inBlock = false, inVerbatim = false;
        int rawQuotes = 0, comments = 0;
        foreach (string line in lines)
        {
            if (!inVerbatim && rawQuotes == 0)
            {
                string trimmed = line.TrimStart();
                if (inBlock || trimmed.StartsWith("//") || trimmed.StartsWith("/*"))
                {
                    comments++;
                }
            }
            Scan(line, ref inBlock, ref inVerbatim, ref rawQuotes);
        }
        return comments;
    }

    private static void Scan(string line, ref bool inBlock, ref bool inVerbatim, ref int rawQuotes)
    {
        for (int i = 0; i < line.Length;)
        {
            if (rawQuotes > 0)
            {
                if (line[i] != '"') { i++; continue; }
                int run = QuoteRun(line, i);
                if (run >= rawQuotes) { rawQuotes = 0; }
                i += run;
            }
            else if (inVerbatim)
            {
                if (line[i] != '"') { i++; continue; }
                if (i + 1 < line.Length && line[i + 1] == '"') { i += 2; continue; }
                inVerbatim = false;
                i++;
            }
            else if (inBlock)
            {
                int end = line.IndexOf("*/", i, StringComparison.Ordinal);
                if (end < 0) { return; }
                inBlock = false;
                i = end + 2;
            }
            else if (line[i] == '/' && i + 1 < line.Length && line[i + 1] == '/')
            {
                return;
            }
            else if (line[i] == '/' && i + 1 < line.Length && line[i + 1] == '*')
            {
                inBlock = true;
                i += 2;
            }
            else if (line[i] == '@' && i + 1 < line.Length && line[i + 1] == '"')
            {
                inVerbatim = true;
                i += 2;
            }
            else if (line[i] == '"')
            {
                int run = QuoteRun(line, i);
                if (run >= 3) { rawQuotes = run; i += run; }
                else if (run == 2) { i += 2; }
                else { i = SkipLiteral(line, i, '"'); }
            }
            else if (line[i] == '\'')
            {
                i = SkipLiteral(line, i, '\'');
            }
            else
            {
                i++;
            }
        }
    }

    private static int QuoteRun(string line, int i)
    {
        int j = i;
        while (j < line.Length && line[j] == '"') { j++; }
        return j - i;
    }

    private static int SkipLiteral(string line, int i, char quote)
    {
        for (int j = i + 1; j < line.Length; j++)
        {
            if (line[j] == '\\') { j++; }
            else if (line[j] == quote) { return j + 1; }
        }
        return line.Length;
    }
}
