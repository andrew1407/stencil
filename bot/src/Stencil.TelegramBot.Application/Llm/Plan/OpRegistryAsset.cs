using System.Text.Json;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Application.Llm.Plan;

// One op the registry lists for the bot: its §13 bullet, or the entry whose bullet it rides.
public sealed record RegistryOp(string Name, string? Bullet, string? BulletSharedWith);

// The embedded opRegistry.json, read only for what the bot says and executes — the §13 bullets, the
// forbidden names, the §11 ask numbers. Core validates plans against the CLI's own copy.
public static class OpRegistryAsset
{
    private const string _resourceName = "Stencil.TelegramBot.Application.Assets.opRegistry.json";

    public const string SURFACE = "bot";

    public static readonly byte[] Bytes = load();

    // FNV-1a 64 over the raw bytes, 16 lowercase hex digits: the CLI's registryFnv1a64.
    public static readonly string Fnv1a64 = Fnv1a64Of(Bytes);

    private static readonly JsonElement _root = parse(Bytes);

    private static readonly string _profile =
        _root.GetProperty("$meta").GetProperty("surfaceProfiles").GetProperty(SURFACE).GetString()!;

    // In the profile's prompt order.
    public static readonly IReadOnlyList<RegistryOp> Ops = readOps();

    public static readonly IReadOnlySet<string> Forbidden =
        _root.GetProperty("forbidden").GetProperty("perSurface").TryGetProperty(SURFACE, out JsonElement f)
            ? f.EnumerateArray().Select(static x => x.GetString()!).ToHashSet(StringComparer.Ordinal)
            : new HashSet<string>(StringComparer.Ordinal);

    public static readonly int AskMaxOptions = askNumber("maxOptions");

    public static readonly int AskMaxAnswer = askNumber("answer");

    public static readonly string DefaultCustomLabel =
        _root.GetProperty("ask").GetProperty("defaultCustomLabel").GetString()!;

    // A mismatch means the CLI and the bot were built from different registries.
    public static bool Matches(PlanCheck check) =>
        check.RegistryBytes == Bytes.Length && string.Equals(check.RegistryFnv1a64, Fnv1a64, StringComparison.Ordinal);

    public static string Describe(PlanCheck check) =>
        $"the CLI embeds an opRegistry.json of {check.RegistryBytes} bytes (FNV-1a {check.RegistryFnv1a64}), "
        + $"the bot one of {Bytes.Length} bytes (FNV-1a {Fnv1a64})";

    private static byte[] load()
    {
        using Stream stream = typeof(OpRegistryAsset).Assembly.GetManifestResourceStream(_resourceName)
            ?? throw new InvalidOperationException($"embedded resource {_resourceName} is missing");
        using MemoryStream copy = new();
        stream.CopyTo(copy);
        return copy.ToArray();
    }

    private static JsonElement parse(byte[] bytes)
    {
        using JsonDocument doc = JsonDocument.Parse(bytes);
        return doc.RootElement.Clone();
    }

    public static string Fnv1a64Of(ReadOnlySpan<byte> bytes)
    {
        ulong hash = 0xcbf29ce484222325;
        foreach (byte b in bytes)
        {
            hash = (hash ^ b) * 0x100000001b3;
        }
        return hash.ToString("x16");
    }

    private static int askNumber(string name) =>
        _root.GetProperty("limits").GetProperty("ask").GetProperty(name).GetInt32();

    // An entry is the bot's when its profiles name the bot's and its surfaces, if any, name the bot.
    private static IReadOnlyList<RegistryOp> readOps()
    {
        List<string> order = [.. _root.GetProperty("profiles").GetProperty(_profile).GetProperty("ops")
            .EnumerateArray().Select(static o => o.GetString()!)];
        List<RegistryOp> ops = new();
        foreach (JsonElement e in _root.GetProperty("ops").EnumerateArray())
        {
            if (!lists(e, "profiles", _profile) || (e.TryGetProperty("surfaces", out _) && !lists(e, "surfaces", SURFACE)))
            {
                continue;
            }
            ops.Add(new RegistryOp(e.GetProperty("name").GetString()!, bullet(e), text(e, "bulletSharedWith")));
        }
        return [.. ops.OrderBy(o => order.IndexOf(o.Name))];
    }

    private static string? bullet(JsonElement e) =>
        e.TryGetProperty("bulletVariants", out JsonElement variants)
        && (text(variants, SURFACE) ?? text(variants, _profile)) is string variant
            ? variant
            : text(e, "bullet");

    private static bool lists(JsonElement e, string list, string value) =>
        e.TryGetProperty(list, out JsonElement arr)
        && arr.EnumerateArray().Any(x => x.ValueKind == JsonValueKind.String && x.GetString() == value);

    private static string? text(JsonElement e, string key) =>
        e.TryGetProperty(key, out JsonElement v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;
}
