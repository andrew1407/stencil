using System.Text.Json;

namespace Stencil.TelegramBot.Application.Llm.Schema;

public sealed class OpSchemaException : Exception
{
    public OpSchemaException(string message) : base(message) { }
}

// One registry op entry resolved for this surface; the holder element carries the cross-field
// rules.
public sealed record OpEntry(
    string Name,
    JsonElement Holder,
    JsonElement Keys,
    IReadOnlyList<string> Rules,
    IReadOnlySet<string> Flags,
    string? Bullet,
    string? BulletSharedWith)
{
    public JsonElement? Spec(string key) =>
        Keys.TryGetProperty(key, out JsonElement spec) ? spec : null;
}

// The bot's port of browser/js/llm/plan/schema.js, rule for rule, over the embedded opRegistry.json.
// The work splits into SchemaLoader, KeySpecChecker, PresenceRules and NativeRules.
public sealed class OpSchema
{
    private static readonly Lazy<OpSchema> _botSchema = new(() => new OpSchema(SchemaLoader.LoadRegistry(), "bot"));

    public static OpSchema Bot => _botSchema.Value;

    private readonly SchemaLoader _loader;
    private readonly KeySpecChecker _checker;

    public string Surface => _loader.Surface;
    public string Profile => _loader.Profile;

    public IReadOnlyList<OpEntry> Entries => _loader.Entries;
    public IReadOnlyDictionary<string, OpEntry> Ops { get; }
    public IReadOnlySet<string> Forbidden => _loader.Forbidden;
    public string DefaultCustomLabel => _loader.DefaultCustomLabel;

    public JsonElement AskKeys => _loader.Registry.GetProperty("ask").GetProperty("schema").GetProperty("keys");

    public JsonElement VariantKeys => _loader.Registry.GetProperty("envelope").GetProperty("variants").GetProperty("items").GetProperty("fields");

    public static JsonElement LoadRegistry() => SchemaLoader.LoadRegistry();

    public OpSchema(JsonElement registry, string surface)
    {
        _loader = new SchemaLoader(registry, surface);
        _checker = new KeySpecChecker(_loader);
        Ops = _loader.Entries.ToDictionary(static e => e.Name, StringComparer.Ordinal);
    }

    public int Limit(JsonElement v) => _checker.Limit(v);

    public int Limit(string name) => _checker.Limit(name);

    // Returns the action as validated (post-fold) — the normalizer reads that one.
    public JsonElement ValidateAction(JsonElement action, OpEntry entry)
    {
        try
        {
            JsonElement v = action;
            foreach (string rule in entry.Rules)
            {
                v = NativeRules.Run(rule, v);
            }
            PresenceRules.CheckFields(_checker, v, entry.Keys, entry.Holder, null, ["op"]);
            return v;
        }
        catch (SchemaError err)
        {
            throw rethrow(err, $"invalid \"{entry.Name}\" action: ");
        }
    }

    // Option actions only shallowly — the caller validates them as preview actions.
    public void ValidateAsk(JsonElement ask)
    {
        try
        {
            if (ask.ValueKind != JsonValueKind.Object)
            {
                throw SchemaError.Bad("\"ask\" must be an object");
            }
            JsonElement schema = _loader.Registry.GetProperty("ask").GetProperty("schema");
            PresenceRules.CheckFields(_checker, ask, schema.GetProperty("keys"), schema, new SchemaPath("ask.", "", null), []);
        }
        catch (SchemaError err)
        {
            throw rethrow(err, "");
        }
    }

    public void CheckEnvelope(JsonElement v, string key)
    {
        try
        {
            _checker.CheckValue(v, _loader.Registry.GetProperty("envelope").GetProperty(key), new SchemaPath("", key, null), null);
        }
        catch (SchemaError err)
        {
            throw rethrow(err, "");
        }
    }

    // null for an unknown op; fail is true for a listed-but-disallowed op.
    public OpEntry? OpsetEntry(string opset, string op, out bool fail)
    {
        fail = false;
        JsonElement os = _loader.Registry.GetProperty("opsets").TryGetProperty(opset, out JsonElement o)
            ? o
            : throw new InvalidOperationException($"opRegistry: unknown opset \"{opset}\"");
        if (os.TryGetProperty("overrides", out JsonElement ov) && ov.TryGetProperty(op, out JsonElement over))
        {
            return new OpEntry(op, over, over.GetProperty("keys"), SchemaLoader.Rules(over), new HashSet<string>(), null, null);
        }
        if (SchemaLoader.Has(os, "ops", op))
        {
            JsonElement core = _loader.Registry.GetProperty("ops").EnumerateArray().FirstOrDefault(e => e.GetProperty("id").GetString() == op);
            return core.ValueKind == JsonValueKind.Object ? _loader.Resolve(core) : null;
        }
        fail = SchemaLoader.Has(os, "failOps", op);
        return null;
    }

    private static OpSchemaException rethrow(SchemaError err, string prefix) => new(prefix + err.Message);
}
