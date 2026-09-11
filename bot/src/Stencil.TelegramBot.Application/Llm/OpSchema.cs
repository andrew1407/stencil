using System.Text.Json;

namespace Stencil.TelegramBot.Application.Llm;

/// <summary>A check failed inside <see cref="OpSchema"/>; the message already carries its prefix.</summary>
public sealed class OpSchemaException : Exception
{
    public OpSchemaException(string message) : base(message) { }
}

/// <summary>
/// One registry op entry resolved for this surface: its key map, native rules, merged flags,
/// prompt bullet (the surface/profile variant when one is recorded) and the holder element
/// carrying the cross-field rules (<c>forms</c> / <c>together</c> / <c>exclusive</c> / <c>minFields</c>).
/// </summary>
public sealed record OpEntry(
    string Name,
    JsonElement Holder,
    JsonElement Keys,
    IReadOnlyList<string> Rules,
    IReadOnlySet<string> Flags,
    string? Bullet,
    string? BulletSharedWith)
{
    /// <summary>The spec of one declared key, or null when the entry does not declare it.</summary>
    public JsonElement? Spec(string key) =>
        Keys.TryGetProperty(key, out JsonElement spec) ? spec : null;
}

/// <summary>
/// The registry-driven op-plan validation engine — the bot's port of
/// <c>browser/js/llm/opSchema.js</c>, rule for rule, over the embedded
/// <c>browser/js/config/llm/opRegistry.json</c>. This is the façade the parser talks to; the
/// work is split into <see cref="SchemaLoader"/> (read + resolve for a surface),
/// <see cref="KeySpecChecker"/> (per-value rules), <see cref="PresenceRules"/> (cross-field
/// rules) and <see cref="NativeRules"/> (the few rules the table cannot express).
/// </summary>
public sealed class OpSchema
{
    private static readonly Lazy<OpSchema> BotSchema = new(() => new OpSchema(SchemaLoader.LoadRegistry(), "bot"));

    /// <summary>The engine for this surface, parsed once.</summary>
    public static OpSchema Bot => BotSchema.Value;

    private readonly SchemaLoader loader;
    private readonly KeySpecChecker checker;

    public string Surface => loader.Surface;
    public string Profile => loader.Profile;

    /// <summary>This surface's entries, in the profile's (= prompt) order.</summary>
    public IReadOnlyList<OpEntry> Entries => loader.Entries;
    public IReadOnlyDictionary<string, OpEntry> Ops { get; }
    public IReadOnlySet<string> Forbidden => loader.Forbidden;
    public string DefaultCustomLabel => loader.DefaultCustomLabel;

    /// <summary>The §11 card's key map (<c>ask.schema.keys</c>).</summary>
    public JsonElement AskKeys => loader.Registry.GetProperty("ask").GetProperty("schema").GetProperty("keys");

    /// <summary>A variant's key map (<c>envelope.variants.items.fields</c>).</summary>
    public JsonElement VariantKeys => loader.Registry.GetProperty("envelope").GetProperty("variants").GetProperty("items").GetProperty("fields");

    public static JsonElement LoadRegistry() => SchemaLoader.LoadRegistry();

    public OpSchema(JsonElement registry, string surface)
    {
        loader = new SchemaLoader(registry, surface);
        checker = new KeySpecChecker(loader);
        Ops = loader.Entries.ToDictionary(static e => e.Name, StringComparer.Ordinal);
    }

    /// <summary>A cap: a number, or a dotted name into <c>limits</c> ("MAX_ACTIONS", "ask.label").</summary>
    public int Limit(JsonElement v) => checker.Limit(v);

    public int Limit(string name) => checker.Limit(name);

    /// <summary>
    /// Validate one action against its entry (native rules first). Returns the action as
    /// validated (post-fold) — the normalizer reads that one.
    /// </summary>
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

    /// <summary>
    /// The §11 card's structure (option <c>actions</c> only shallowly — the caller validates
    /// them as preview actions).
    /// </summary>
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

    /// <summary>Check one envelope slot ("actions" / "variants") shallowly.</summary>
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

    /// <summary>
    /// Resolve an op inside a nested op set: an entry to validate with, null for an unknown
    /// op; <paramref name="fail"/> is true for a listed-but-disallowed op.
    /// </summary>
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
