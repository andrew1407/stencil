using System.Text.RegularExpressions;

namespace Stencil.TelegramBot.Tests;

// The four rings Domain <- Application <- Infrastructure <- Bot, read from each project's
// ProjectReferences and every source file's using lines. Dependencies point inward only.
public sealed partial class LayerBoundaryTests
{
    private const string Prefix = "Stencil.TelegramBot.";

    private static readonly string[] Rings = ["Domain", "Application", "Infrastructure", "Bot"];

    private static readonly IReadOnlyDictionary<string, string[]> AllowedInternal = new Dictionary<string, string[]>
    {
        ["Domain"] = [],
        ["Application"] = ["Domain"],
        ["Infrastructure"] = ["Domain", "Application"],
        ["Bot"] = Rings,
    };

    // Namespaces the Domain may never name: Telegram, HTTP, child processes and Redis belong to the adapters.
    private static readonly string[] DomainForbiddenNamespaces =
        ["Telegram.Bot", "System.Net.Http", "System.Diagnostics.Process", "StackExchange.Redis"];

    private static readonly string[] DomainForbiddenPackages = ["Telegram.Bot", "StackExchange.Redis"];

    // Frozen violations, one reason each; empty means the tree honours every rule.
    private static readonly IReadOnlyDictionary<string, string> Allowances = new Dictionary<string, string>();

    [GeneratedRegex(@"^\s*(?:global\s+)?using\s+(?:static\s+)?(?:[\w.]+\s*=\s*)?([\w.]+)\s*;")]
    private static partial Regex usingLine();

    [GeneratedRegex(@"<ProjectReference\s+Include=""([^""]+)""")]
    private static partial Regex projectReference();

    [GeneratedRegex(@"<PackageReference\s+Include=""([^""]+)""")]
    private static partial Regex packageReference();

    private static string projectDir(string ring) =>
        SharedFixtures.PathOf("bot", "src", Prefix + ring);

    private static string csprojText(string ring) =>
        File.ReadAllText(Path.Combine(projectDir(ring), Prefix + ring + ".csproj"));

    private static IEnumerable<string> sourceFiles(string ring) =>
        Directory.EnumerateFiles(projectDir(ring), "*.cs", SearchOption.AllDirectories)
            .Where(f => !f.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}")
                     && !f.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}"));

    private static string? ringOf(string ns) =>
        ns.StartsWith(Prefix, StringComparison.Ordinal)
            ? Rings.FirstOrDefault(r => ns == Prefix + r || ns.StartsWith(Prefix + r + ".", StringComparison.Ordinal))
            : null;

    private static string toRel(string path) =>
        Path.GetRelativePath(SharedFixtures.RepoRoot, path).Replace('\\', '/');

    private static bool isAllowed(string relPath) => Allowances.ContainsKey(relPath);

    public static IEnumerable<object[]> RingNames() => Rings.Select(r => new object[] { r });

    [Theory]
    [MemberData(nameof(RingNames))]
    public void ProjectReferencesPointInward(string ring)
    {
        string[] allowed = AllowedInternal[ring];
        string[] offending = projectReference().Matches(csprojText(ring))
            .Select(m => Path.GetFileNameWithoutExtension(m.Groups[1].Value.Replace('\\', '/')))
            .Select(name => name[Prefix.Length..])
            .Where(target => target != ring && !allowed.Contains(target))
            .ToArray();
        Assert.True(offending.Length == 0,
            $"{ring} may reference only [{string.Join(", ", allowed)}] but references: {string.Join(", ", offending)}");
    }

    [Theory]
    [MemberData(nameof(RingNames))]
    public void UsingLinesPointInward(string ring)
    {
        string[] allowed = AllowedInternal[ring];
        List<string> offending = [];
        foreach (string file in sourceFiles(ring))
        {
            if (isAllowed(toRel(file)))
            {
                continue;
            }
            foreach (string ns in usings(file))
            {
                string? target = ringOf(ns);
                if (target is not null && target != ring && !allowed.Contains(target))
                {
                    offending.Add($"{toRel(file)}: using {ns}");
                }
            }
        }
        Assert.True(offending.Count == 0,
            $"{ring} may use only [{string.Join(", ", allowed)}]:\n  " + string.Join("\n  ", offending));
    }

    [Fact]
    public void DomainNamesNoAdapterNamespace()
    {
        List<string> offending = [];
        foreach (string file in sourceFiles("Domain"))
        {
            if (isAllowed(toRel(file)))
            {
                continue;
            }
            foreach (string ns in usings(file))
            {
                if (DomainForbiddenNamespaces.Any(f => ns == f || ns.StartsWith(f + ".", StringComparison.Ordinal)))
                {
                    offending.Add($"{toRel(file)}: using {ns}");
                }
            }
        }
        Assert.True(offending.Count == 0, "Domain names an adapter namespace:\n  " + string.Join("\n  ", offending));
    }

    [Fact]
    public void DomainReferencesNoAdapterPackage()
    {
        string[] packages = packageReference().Matches(csprojText("Domain"))
            .Select(m => m.Groups[1].Value)
            .Where(p => DomainForbiddenPackages.Any(f => p.Equals(f, StringComparison.OrdinalIgnoreCase)))
            .ToArray();
        Assert.True(packages.Length == 0, "Domain references an adapter package: " + string.Join(", ", packages));
    }

    [Fact]
    public void EveryAllowanceStillNamesAFile()
    {
        string[] stale = Allowances.Keys
            .Where(rel => !File.Exists(SharedFixtures.PathOf(rel.Split('/'))))
            .ToArray();
        Assert.True(stale.Length == 0, "allowance for a file that no longer exists: " + string.Join(", ", stale));
    }

    [Fact]
    public void EveryRingHasSources()
    {
        foreach (string ring in Rings)
        {
            Assert.True(sourceFiles(ring).Any(), $"no sources found for {ring}");
        }
    }

    private static IEnumerable<string> usings(string file)
    {
        foreach (string line in File.ReadLines(file))
        {
            Match m = usingLine().Match(line);
            if (m.Success)
            {
                yield return m.Groups[1].Value;
            }
        }
    }
}
