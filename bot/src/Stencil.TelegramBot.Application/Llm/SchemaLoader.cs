using System.Text.Json;
using System.Text.RegularExpressions;
using static Stencil.TelegramBot.Application.Llm.SchemaJson;

namespace Stencil.TelegramBot.Application.Llm;

// Resolves the embedded opRegistry.json FOR ONE SURFACE; pure loading, no validation lives here.
internal sealed class SchemaLoader
{
    private const string _resourceName = "Stencil.TelegramBot.Application.Assets.opRegistry.json";

    public JsonElement Registry { get; }
    public string Surface { get; }
    public string Profile { get; }
    public JsonElement Limits { get; }
    public JsonElement Describe { get; }
    public IReadOnlyDictionary<string, Regex> Regexes { get; }
    public IReadOnlyList<OpEntry> Entries { get; }
    public IReadOnlySet<string> Forbidden { get; }
    public string DefaultCustomLabel { get; }

    public static JsonElement LoadRegistry()
    {
        using Stream stream = typeof(SchemaLoader).Assembly.GetManifestResourceStream(_resourceName)
            ?? throw new InvalidOperationException($"embedded resource {_resourceName} is missing");
        using JsonDocument doc = JsonDocument.Parse(stream);
        return doc.RootElement.Clone();
    }

    public SchemaLoader(JsonElement registry, string surface)
    {
        Registry = registry;
        Surface = surface;
        Profile = registry.GetProperty("$meta").GetProperty("surfaceProfiles").TryGetProperty(surface, out JsonElement p)
            ? p.GetString()!
            : throw new InvalidOperationException($"opRegistry: unknown surface \"{surface}\"");
        Limits = registry.GetProperty("limits");
        JsonElement rx = registry.GetProperty("regexes");
        Describe = rx.GetProperty("describe");
        Dictionary<string, Regex> regexes = new(StringComparer.Ordinal);
        foreach (JsonProperty prop in rx.EnumerateObject())
        {
            if (prop.Name is not ("describe" or "note"))
            {
                regexes[prop.Name] = compile(prop.Value.GetString()!);
            }
        }
        Regexes = regexes;

        List<string> order = registry.GetProperty("profiles").GetProperty(Profile).GetProperty("ops")
            .EnumerateArray().Select(static o => o.GetString()!).ToList();
        List<OpEntry> entries = new();
        foreach (JsonElement e in registry.GetProperty("ops").EnumerateArray())
        {
            if (!Has(e, "profiles", Profile) || (e.TryGetProperty("surfaces", out JsonElement s) && !Has(e, "surfaces", surface)))
            {
                continue;
            }
            entries.Add(Resolve(e));
        }
        entries.Sort((a, b) => order.IndexOf(a.Name).CompareTo(order.IndexOf(b.Name)));
        Entries = entries;
        Forbidden = registry.GetProperty("forbidden").GetProperty("perSurface").TryGetProperty(surface, out JsonElement f)
            ? f.EnumerateArray().Select(static x => x.GetString()!).ToHashSet(StringComparer.Ordinal)
            : new HashSet<string>(StringComparer.Ordinal);
        DefaultCustomLabel = registry.GetProperty("ask").GetProperty("defaultCustomLabel").GetString()!;
    }

    // JS "$" anchors only at the very end; .NET's also matches before a final newline.
    // ECMAScript mode keeps \d ASCII-only, like the reference engine.
    private static Regex compile(string source)
    {
        string src = source.EndsWith('$') && !source.EndsWith("\\$", StringComparison.Ordinal)
            ? source[..^1] + "\\z"
            : source;
        return new Regex(src, RegexOptions.ECMAScript | RegexOptions.CultureInvariant);
    }

    public static bool Has(JsonElement e, string list, string value) =>
        e.TryGetProperty(list, out JsonElement arr)
        && arr.EnumerateArray().Any(x => x.ValueKind == JsonValueKind.String && x.GetString() == value);

    public OpEntry Resolve(JsonElement e)
    {
        string name = e.GetProperty("name").GetString()!;
        JsonElement keys = e.TryGetProperty("surfaceKeys", out JsonElement sk) && sk.TryGetProperty(Surface, out JsonElement mine)
            ? mine
            : e.TryGetProperty("keys", out JsonElement k) ? k : EmptyObject;
        string? bullet = e.TryGetProperty("bullet", out JsonElement b) && b.ValueKind == JsonValueKind.String ? b.GetString() : null;
        if (e.TryGetProperty("bulletVariants", out JsonElement bv) && forSurface(bv) is JsonElement variant
            && variant.ValueKind == JsonValueKind.String)
        {
            bullet = variant.GetString();
        }
        HashSet<string> flags = new(StringComparer.Ordinal);
        if (e.TryGetProperty("flags", out JsonElement fl))
        {
            addFlags(flags, fl);
        }
        if (e.TryGetProperty("surfaceFlags", out JsonElement sf) && forSurface(sf) is JsonElement extra)
        {
            addFlags(flags, extra);
        }
        return new OpEntry(name, e, keys, Rules(e), flags, bullet,
            e.TryGetProperty("bulletSharedWith", out JsonElement shared) && shared.ValueKind == JsonValueKind.String ? shared.GetString() : null);
    }

    private JsonElement? forSurface(JsonElement map) =>
        map.TryGetProperty(Surface, out JsonElement s) ? s : map.TryGetProperty(Profile, out JsonElement p) ? p : null;

    private static void addFlags(HashSet<string> flags, JsonElement map)
    {
        foreach (JsonProperty prop in map.EnumerateObject())
        {
            if (prop.Value.ValueKind == JsonValueKind.True)
            {
                flags.Add(prop.Name);
            }
        }
    }

    public static string[] Rules(JsonElement holder) =>
        holder.TryGetProperty("rules", out JsonElement r) && r.ValueKind == JsonValueKind.Array
            ? r.EnumerateArray().Select(static x => x.GetString()!).ToArray()
            : [];
}
