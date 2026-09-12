using System.Text.Json;

namespace Stencil.TelegramBot.Application.Llm;

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

// The bot's port of browser/js/llm/opSchema.js, rule for rule, over the embedded opRegistry.json.
// The work splits into SchemaLoader, KeySpecChecker, PresenceRules and NativeRules.
public sealed class OpSchema
{
    private static readonly Lazy<OpSchema> BotSchema = new(() => new OpSchema(SchemaLoader.LoadRegistry(), "bot"));

    public static OpSchema Bot => BotSchema.Value;

    private readonly SchemaLoader loader;
    private readonly KeySpecChecker checker;

    public string Surface => loader.Surface;
    public string Profile => loader.Profile;

    public IReadOnlyList<OpEntry> Entries => loader.Entries;
    public IReadOnlyDictionary<string, OpEntry> Ops { get; }
    public IReadOnlySet<string> Forbidden => loader.Forbidden;
    public string DefaultCustomLabel => loader.DefaultCustomLabel;

    public JsonElement AskKeys => loader.Registry.GetProperty("ask").GetProperty("schema").GetProperty("keys");

    public JsonElement VariantKeys => loader.Registry.GetProperty("envelope").GetProperty("variants").GetProperty("items").GetProperty("fields");

    public static JsonElement LoadRegistry() => SchemaLoader.LoadRegistry();

    public OpSchema(JsonElement registry, string surface)
    {
        loader = new SchemaLoader(registry, surface);
        checker = new KeySpecChecker(loader);
        Ops = loader.Entries.ToDictionary(static e => e.Name, StringComparer.Ordinal);
    }

    public int Limit(JsonElement v) => checker.Limit(v);

    public int Limit(string name) => checker.Limit(name);

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
            PresenceRules.CheckFields(checker, v, entry.Keys, entry.Holder, null, ["op"]);
            return v;
        }
        catch (SchemaError err)
        {
            throw Rethrow(err, $"invalid \"{entry.Name}\" action: ");
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
            JsonElement schema = loader.Registry.GetProperty("ask").GetProperty("schema");
            PresenceRules.CheckFields(checker, ask, schema.GetProperty("keys"), schema, new SchemaPath("ask.", "", null), []);
        }
        catch (SchemaError err)
        {
            throw Rethrow(err, "");
        }
    }

    public void CheckEnvelope(JsonElement v, string key)
    {
        try
        {
            checker.CheckValue(v, loader.Registry.GetProperty("envelope").GetProperty(key), new SchemaPath("", key, null), null);
        }
        catch (SchemaError err)
        {
            throw Rethrow(err, "");
        }
    }

    // null for an unknown op; fail is true for a listed-but-disallowed op.
    public OpEntry? OpsetEntry(string opset, string op, out bool fail)
    {
        fail = false;
        JsonElement os = loader.Registry.GetProperty("opsets").TryGetProperty(opset, out JsonElement o)
            ? o
            : throw new InvalidOperationException($"opRegistry: unknown opset \"{opset}\"");
        if (os.TryGetProperty("overrides", out JsonElement ov) && ov.TryGetProperty(op, out JsonElement over))
        {
            return new OpEntry(op, over, over.GetProperty("keys"), SchemaLoader.Rules(over), new HashSet<string>(), null, null);
        }
        if (SchemaLoader.Has(os, "ops", op))
        {
            JsonElement core = loader.Registry.GetProperty("ops").EnumerateArray().FirstOrDefault(e => e.GetProperty("id").GetString() == op);
            return core.ValueKind == JsonValueKind.Object ? loader.Resolve(core) : null;
        }
        fail = SchemaLoader.Has(os, "failOps", op);
        return null;
    }

    private static OpSchemaException Rethrow(SchemaError err, string prefix) => new(prefix + err.Message);
}
