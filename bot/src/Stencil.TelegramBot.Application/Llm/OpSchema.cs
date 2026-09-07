using System.Globalization;
using System.Text.Json;
using System.Text.RegularExpressions;

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
/// <c>browser/js/config/llm/opRegistry.json</c>: profile membership, unknown-field rejection,
/// required keys, types, enums, ranges, string caps, token grammars and the cross-field
/// presence rules. The parser keeps only its normalizers and the native rules named in an
/// entry's <c>rules</c>.
/// </summary>
public sealed class OpSchema
{
    private const string ResourceName = "Stencil.TelegramBot.Application.Assets.opRegistry.json";

    private static readonly Lazy<OpSchema> BotSchema = new(() => new OpSchema(LoadRegistry(), "bot"));

    /// <summary>The engine for this surface, parsed once.</summary>
    public static OpSchema Bot => BotSchema.Value;

    private readonly JsonElement registry;
    private readonly JsonElement limits;
    private readonly Dictionary<string, Regex> regexes = new(StringComparer.Ordinal);
    private readonly JsonElement describe;

    public string Surface { get; }
    public string Profile { get; }

    /// <summary>This surface's entries, in the profile's (= prompt) order.</summary>
    public IReadOnlyList<OpEntry> Entries { get; }
    public IReadOnlyDictionary<string, OpEntry> Ops { get; }
    public IReadOnlySet<string> Forbidden { get; }
    public string DefaultCustomLabel { get; }

    /// <summary>The §11 card's key map (<c>ask.schema.keys</c>).</summary>
    public JsonElement AskKeys => registry.GetProperty("ask").GetProperty("schema").GetProperty("keys");

    /// <summary>A variant's key map (<c>envelope.variants.items.fields</c>).</summary>
    public JsonElement VariantKeys => registry.GetProperty("envelope").GetProperty("variants").GetProperty("items").GetProperty("fields");

    public static JsonElement LoadRegistry()
    {
        using Stream stream = typeof(OpSchema).Assembly.GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException($"embedded resource {ResourceName} is missing");
        using JsonDocument doc = JsonDocument.Parse(stream);
        return doc.RootElement.Clone();
    }

    public OpSchema(JsonElement registry, string surface)
    {
        this.registry = registry;
        Surface = surface;
        Profile = registry.GetProperty("$meta").GetProperty("surfaceProfiles").TryGetProperty(surface, out JsonElement p)
            ? p.GetString()!
            : throw new InvalidOperationException($"opRegistry: unknown surface \"{surface}\"");
        limits = registry.GetProperty("limits");
        JsonElement rx = registry.GetProperty("regexes");
        describe = rx.GetProperty("describe");
        foreach (JsonProperty prop in rx.EnumerateObject())
        {
            if (prop.Name is not ("describe" or "note"))
            {
                regexes[prop.Name] = Compile(prop.Value.GetString()!);
            }
        }

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
        Ops = entries.ToDictionary(static e => e.Name, StringComparer.Ordinal);
        Forbidden = registry.GetProperty("forbidden").GetProperty("perSurface").TryGetProperty(surface, out JsonElement f)
            ? f.EnumerateArray().Select(static x => x.GetString()!).ToHashSet(StringComparer.Ordinal)
            : new HashSet<string>(StringComparer.Ordinal);
        DefaultCustomLabel = registry.GetProperty("ask").GetProperty("defaultCustomLabel").GetString()!;
    }

    // JS "$" anchors only at the very end; .NET's also matches before a final newline.
    // ECMAScript mode keeps \d ASCII-only, like the reference engine.
    private static Regex Compile(string source)
    {
        string src = source.EndsWith('$') && !source.EndsWith("\\$", StringComparison.Ordinal)
            ? source[..^1] + "\\z"
            : source;
        return new Regex(src, RegexOptions.ECMAScript | RegexOptions.CultureInvariant);
    }

    private static bool Has(JsonElement e, string list, string value) =>
        e.TryGetProperty(list, out JsonElement arr)
        && arr.EnumerateArray().Any(x => x.ValueKind == JsonValueKind.String && x.GetString() == value);

    private OpEntry Resolve(JsonElement e)
    {
        string name = e.GetProperty("name").GetString()!;
        JsonElement keys = e.TryGetProperty("surfaceKeys", out JsonElement sk) && sk.TryGetProperty(Surface, out JsonElement mine)
            ? mine
            : e.TryGetProperty("keys", out JsonElement k) ? k : EmptyObject;
        string? bullet = e.TryGetProperty("bullet", out JsonElement b) && b.ValueKind == JsonValueKind.String ? b.GetString() : null;
        if (e.TryGetProperty("bulletVariants", out JsonElement bv) && ForSurface(bv) is JsonElement variant
            && variant.ValueKind == JsonValueKind.String)
        {
            bullet = variant.GetString();
        }
        HashSet<string> flags = new(StringComparer.Ordinal);
        if (e.TryGetProperty("flags", out JsonElement fl))
        {
            AddFlags(flags, fl);
        }
        if (e.TryGetProperty("surfaceFlags", out JsonElement sf) && ForSurface(sf) is JsonElement extra)
        {
            AddFlags(flags, extra);
        }
        return new OpEntry(name, e, keys, Rules(e), flags, bullet,
            e.TryGetProperty("bulletSharedWith", out JsonElement shared) && shared.ValueKind == JsonValueKind.String ? shared.GetString() : null);
    }

    private JsonElement? ForSurface(JsonElement map) =>
        map.TryGetProperty(Surface, out JsonElement s) ? s : map.TryGetProperty(Profile, out JsonElement p) ? p : null;

    private static void AddFlags(HashSet<string> flags, JsonElement map)
    {
        foreach (JsonProperty prop in map.EnumerateObject())
        {
            if (prop.Value.ValueKind == JsonValueKind.True)
            {
                flags.Add(prop.Name);
            }
        }
    }

    private static string[] Rules(JsonElement holder) =>
        holder.TryGetProperty("rules", out JsonElement r) && r.ValueKind == JsonValueKind.Array
            ? r.EnumerateArray().Select(static x => x.GetString()!).ToArray()
            : [];

    /// <summary>A cap: a number, or a dotted name into <c>limits</c> ("MAX_ACTIONS", "ask.label").</summary>
    public int Limit(JsonElement v)
    {
        if (v.ValueKind == JsonValueKind.Number)
        {
            return (int)v.GetDouble();
        }
        return Limit(v.GetString()!);
    }

    public int Limit(string name)
    {
        JsonElement at = limits;
        foreach (string part in name.Split('.'))
        {
            if (at.ValueKind != JsonValueKind.Object || !at.TryGetProperty(part, out at))
            {
                throw new InvalidOperationException($"opRegistry: unknown limit \"{name}\"");
            }
        }
        return at.ValueKind == JsonValueKind.Number ? (int)at.GetDouble() : throw new InvalidOperationException($"opRegistry: unknown limit \"{name}\"");
    }

    private string Describe(string name) =>
        describe.TryGetProperty(name, out JsonElement d) ? d.GetString()! : name;

    // ── paths for messages: "x1" in spec, "label" in ask.options[2] ──
    private sealed record Path(string Root, string Key, string? Container);

    private static string Where(Path p) =>
        p.Key.Length > 0 ? $"{(p.Container is null ? "" : p.Container + ".")}{p.Root}{p.Key}" : p.Root.TrimEnd('.');

    private static string Label(Path p) => $"\"{p.Root}{p.Key}\"" + (p.Container is null ? "" : $" in {p.Container}");

    private static Path Child(Path? p, string key) =>
        p is not null && p.Key.Length > 0 ? new("", key, Where(p)) : new(p?.Root ?? "", key, null);

    private static Path Item(Path p, int i) => new(p.Root, $"{p.Key}[{i}]", p.Container);

    private sealed class SchemaError : Exception
    {
        public SchemaError(string why) : base(why) { }
    }

    private static Exception Bad(string why) => new SchemaError(why);

    private static string QuoteList(JsonElement list) =>
        string.Join(", ", list.EnumerateArray().Select(static x => x.ValueKind == JsonValueKind.String ? $"\"{x.GetString()}\"" : x.GetRawText()));

    private static bool IsPresent(JsonElement obj, string key) =>
        obj.TryGetProperty(key, out JsonElement v) && v.ValueKind != JsonValueKind.Null;

    private static bool IsFiniteNumber(JsonElement v, out double d)
    {
        d = 0;
        return v.ValueKind == JsonValueKind.Number && v.TryGetDouble(out d) && double.IsFinite(d);
    }

    private static bool JsonEquals(JsonElement a, JsonElement b) => a.ValueKind switch
    {
        JsonValueKind.String => b.ValueKind == JsonValueKind.String && a.GetString() == b.GetString(),
        JsonValueKind.Number => b.ValueKind == JsonValueKind.Number && a.GetDouble() == b.GetDouble(),
        JsonValueKind.True or JsonValueKind.False => a.ValueKind == b.ValueKind,
        _ => false,
    };

    private static bool Contains(JsonElement list, JsonElement v) => list.EnumerateArray().Any(x => JsonEquals(x, v));

    private static bool Flag(JsonElement spec, string name) =>
        spec.TryGetProperty(name, out JsonElement f) && f.ValueKind == JsonValueKind.True;

    // ── value checks ──
    private void CheckString(JsonElement v, JsonElement spec, Path path, JsonElement? parent)
    {
        if (v.ValueKind != JsonValueKind.String)
        {
            throw Bad($"{Label(path)} must be a string");
        }
        string raw = v.GetString()!;
        int max = spec.TryGetProperty("maxChars", out JsonElement mc) ? Limit(mc) : Limit("MAX_STRING_CHARS");
        if (raw.Length > max)
        {
            throw Bad($"{Label(path)} is longer than {max} characters");
        }
        string s = Flag(spec, "trim") ? raw.Trim() : raw;
        if (Flag(spec, "nonEmpty") && s.Trim().Length == 0)
        {
            throw Bad($"{Label(path)} must be a non-empty string");
        }
        if (spec.TryGetProperty("enum", out JsonElement en) && !en.EnumerateArray().Any(x => x.GetString() == s))
        {
            throw Bad($"{Label(path)} must be one of {QuoteList(en)}");
        }
        if (spec.TryGetProperty("literals", out JsonElement lit) && lit.EnumerateArray().Any(x => x.GetString() == s))
        {
            return;
        }
        if (Flag(spec, "blankOk") && s.Trim().Length == 0)
        {
            return;
        }
        List<string> names = new();
        if (spec.TryGetProperty("regex", out JsonElement rx))
        {
            names.AddRange(rx.ValueKind == JsonValueKind.Array ? rx.EnumerateArray().Select(static x => x.GetString()!) : [rx.GetString()!]);
        }
        if (spec.TryGetProperty("regexBy", out JsonElement by))
        {
            names.Clear();
            string depKey = by.GetProperty("key").GetString()!;
            if (parent is JsonElement pe && pe.TryGetProperty(depKey, out JsonElement dep) && dep.ValueKind == JsonValueKind.String
                && by.GetProperty("map").TryGetProperty(dep.GetString()!, out JsonElement mapped))
            {
                names.Add(mapped.GetString()!);
            }
        }
        if (names.Count > 0 && !names.Any(n => regexes[n].IsMatch(s)))
        {
            throw Bad($"{Label(path)} must be {string.Join(" or ", names.Select(Describe))}");
        }
        if (spec.TryGetProperty("regexNot", out JsonElement not) && regexes[not.GetString()!].IsMatch(s))
        {
            throw Bad($"{Label(path)} must be a local value, not {Describe(not.GetString()!)}");
        }
    }

    private static void CheckNumber(JsonElement v, JsonElement spec, Path path)
    {
        bool integer = spec.GetProperty("type").GetString() == "integer";
        string noun = integer ? "an integer" : "a number";
        if (!IsFiniteNumber(v, out double d) || (integer && Math.Floor(d) != d))
        {
            throw Bad($"{Label(path)} must be {noun}");
        }
        if (spec.TryGetProperty("enum", out JsonElement en) && !Contains(en, v))
        {
            throw Bad($"{Label(path)} must be one of {QuoteList(en)}");
        }
        if (spec.TryGetProperty("range", out JsonElement range))
        {
            JsonElement lo = range[0], hi = range[1];
            bool hasLo = lo.ValueKind == JsonValueKind.Number, hasHi = hi.ValueKind == JsonValueKind.Number;
            if ((hasLo && d < lo.GetDouble()) || (hasHi && d > hi.GetDouble()))
            {
                string bounds = hasLo && hasHi ? $"{lo.GetRawText()}..{hi.GetRawText()}" : hasLo ? $">= {lo.GetRawText()}" : $"<= {hi.GetRawText()}";
                throw Bad($"{Label(path)} must be {noun} {bounds}");
            }
        }
    }

    private static void CheckBoolean(JsonElement v, JsonElement spec, Path path)
    {
        if (v.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
        {
            throw Bad($"{Label(path)} must be a boolean");
        }
        if (spec.TryGetProperty("enum", out JsonElement en) && !Contains(en, v))
        {
            throw Bad($"{Label(path)} must be {QuoteList(en)}");
        }
    }

    private void CheckArray(JsonElement v, JsonElement spec, Path path)
    {
        if (v.ValueKind != JsonValueKind.Array)
        {
            throw Bad($"{Label(path)} must be an array");
        }
        int? min = spec.TryGetProperty("minItems", out JsonElement mi) ? Limit(mi) : null;
        int? max = spec.TryGetProperty("maxItems", out JsonElement ma) ? Limit(ma) : null;
        int n = v.GetArrayLength();
        if (min == 1 && n == 0)
        {
            throw Bad($"{Label(path)} must be a non-empty array");
        }
        bool window = min is > 1 && max is not null;   // a real N..M window, not just a cap
        if (max is int hi && n > hi)
        {
            throw Bad(window ? $"{Label(path)} must hold {min}..{max} entries" : $"more than {max} entries in {Label(path)}");
        }
        if (min is int lo && n < lo)
        {
            throw Bad(window ? $"{Label(path)} must hold {min}..{max} entries" : $"{Label(path)} must hold at least {min} entries");
        }
        if (spec.TryGetProperty("items", out JsonElement items))
        {
            int i = 0;
            foreach (JsonElement x in v.EnumerateArray())
            {
                CheckValue(x, items, Item(path, i++), null);
            }
        }
    }

    private void CheckObject(JsonElement v, JsonElement spec, Path path)
    {
        if (v.ValueKind != JsonValueKind.Object)
        {
            throw Bad($"{Label(path)} must be an object");
        }
        bool hasFields = spec.TryGetProperty("fields", out JsonElement fields);
        if (hasFields || spec.TryGetProperty("minFields", out _))
        {
            CheckFields(v, hasFields ? fields : EmptyObject, spec, path, []);
        }
    }

    private static readonly JsonElement EmptyObject = JsonDocument.Parse("{}").RootElement.Clone();

    private void CheckValue(JsonElement v, JsonElement spec, Path path, JsonElement? parent)
    {
        switch (spec.GetProperty("type").GetString())
        {
            case "string": CheckString(v, spec, path, parent); break;
            case "integer":
            case "number": CheckNumber(v, spec, path); break;
            case "boolean": CheckBoolean(v, spec, path); break;
            case "array": CheckArray(v, spec, path); break;
            case "object": CheckObject(v, spec, path); break;
            default: throw new InvalidOperationException($"opRegistry: unknown type \"{spec.GetProperty("type")}\"");
        }
    }

    /// <summary>
    /// One object against a key map + its holder's presence rules. <paramref name="skip"/>
    /// names keys that are neither declared nor unknown (the action's own "op").
    /// </summary>
    private void CheckFields(JsonElement obj, JsonElement fields, JsonElement holder, Path? path, string[] skip)
    {
        // An object spec with allowUnknown (the envelope's variant objects) tolerates undeclared keys.
        if (!Flag(holder, "allowUnknown"))
        {
            foreach (JsonProperty prop in obj.EnumerateObject())
            {
                if (Array.IndexOf(skip, prop.Name) < 0 && !fields.TryGetProperty(prop.Name, out _))
                {
                    throw Bad($"unknown field \"{prop.Name}\"{(path is null ? "" : $" in {Where(path)}")}");
                }
            }
        }
        List<string> declared = fields.EnumerateObject().Select(static f => f.Name).ToList();
        if (holder.TryGetProperty("forms", out JsonElement forms))
        {
            List<string[]> groups = forms.EnumerateArray().Select(static g => g.EnumerateArray().Select(static k => k.GetString()!).ToArray()).ToList();
            HashSet<string> inForms = groups.SelectMany(static g => g).ToHashSet(StringComparer.Ordinal);
            List<string> given = declared.Where(k => inForms.Contains(k) && IsPresent(obj, k)).ToList();
            int matched = groups.Count(g => g.Length == given.Count && g.All(given.Contains));
            if (matched != 1)
            {
                throw Bad($"exactly one of {string.Join(" / ", groups.Select(static g => string.Join("+", g.Select(static k => $"\"{k}\""))))} is required");
            }
        }
        if (holder.TryGetProperty("together", out JsonElement together))
        {
            foreach (JsonElement g in together.EnumerateArray())
            {
                string[] group = g.EnumerateArray().Select(static k => k.GetString()!).ToArray();
                int n = group.Count(k => IsPresent(obj, k));
                if (n > 0 && n != group.Length)
                {
                    throw Bad($"{string.Join(" and ", group.Select(static k => $"\"{k}\""))} ride together");
                }
            }
        }
        if (holder.TryGetProperty("exclusive", out JsonElement exclusive))
        {
            foreach (JsonElement g in exclusive.EnumerateArray())
            {
                string[] group = g.EnumerateArray().Select(static k => k.GetString()!).ToArray();
                if (group.Count(k => IsPresent(obj, k)) > 1)
                {
                    throw Bad($"carries both {string.Join(" and ", group.Select(static k => $"\"{k}\""))} — at most one of them");
                }
            }
        }
        if (holder.TryGetProperty("minFields", out JsonElement mf))
        {
            int min = mf.GetInt32();
            if (declared.Count(k => IsPresent(obj, k)) < min)
            {
                throw Bad($"needs at least {(min == 1 ? "one" : min.ToString(CultureInfo.InvariantCulture))} of {string.Join("/", declared)}");
            }
        }
        foreach (JsonProperty field in fields.EnumerateObject())
        {
            string k = field.Name;
            JsonElement spec = field.Value;
            Path at = Child(path, k);
            if (!IsPresent(obj, k))
            {
                if (Flag(spec, "required"))
                {
                    throw Bad($"{Label(at)} is required");
                }
                if (spec.TryGetProperty("requiredWith", out JsonElement rw))
                {
                    foreach (JsonProperty dep in rw.EnumerateObject())
                    {
                        if (obj.TryGetProperty(dep.Name, out JsonElement dv) && Contains(dep.Value, dv))
                        {
                            throw Bad($"{Label(at)} is required with \"{dep.Name}\" {QuoteList(Single(dv))}");
                        }
                    }
                }
                continue;
            }
            if (spec.TryGetProperty("onlyWith", out JsonElement ow))
            {
                foreach (JsonProperty dep in ow.EnumerateObject())
                {
                    if (!(obj.TryGetProperty(dep.Name, out JsonElement dv) && Contains(dep.Value, dv)))
                    {
                        throw Bad($"{Label(at)} only applies with \"{dep.Name}\" {string.Join(" or ", dep.Value.EnumerateArray().Select(static x => $"\"{x.GetString()}\""))}");
                    }
                }
            }
            CheckValue(obj.GetProperty(k), spec, at, obj);
        }
    }

    private static JsonElement Single(JsonElement v)
    {
        using JsonDocument doc = JsonDocument.Parse($"[{v.GetRawText()}]");
        return doc.RootElement.Clone();
    }

    // ── native cross-field rules an entry may name in `rules` ──
    // §3.2 tolerance: "aspect" beside "spec" folds into the spec when it lacks one; a
    // conflicting duplicate fails. The folded copy is what gets validated + normalized.
    private static JsonElement CropAspectFold(JsonElement a)
    {
        if (!IsPresent(a, "aspect") || !a.TryGetProperty("spec", out JsonElement spec) || spec.ValueKind != JsonValueKind.Object)
        {
            return a;
        }
        JsonElement beside = a.GetProperty("aspect");
        if (IsPresent(spec, "aspect") && !JsonEquals(spec.GetProperty("aspect"), beside))
        {
            throw Bad("\"aspect\" appears both beside \"spec\" and inside it with conflicting values");
        }
        using MemoryStream buffer = new();
        using (Utf8JsonWriter w = new(buffer))
        {
            w.WriteStartObject();
            foreach (JsonProperty prop in a.EnumerateObject())
            {
                if (prop.Name == "aspect")
                {
                    continue;
                }
                if (prop.Name != "spec")
                {
                    prop.WriteTo(w);
                    continue;
                }
                w.WritePropertyName("spec");
                w.WriteStartObject();
                foreach (JsonProperty sp in spec.EnumerateObject())
                {
                    if (sp.Name != "aspect" || sp.Value.ValueKind != JsonValueKind.Null)
                    {
                        sp.WriteTo(w);
                    }
                }
                if (!IsPresent(spec, "aspect"))
                {
                    w.WritePropertyName("aspect");
                    beside.WriteTo(w);
                }
                w.WriteEndObject();
            }
            w.WriteEndObject();
        }
        using JsonDocument doc = JsonDocument.Parse(buffer.ToArray());
        return doc.RootElement.Clone();
    }

    private static JsonElement RunRule(string rule, JsonElement a) => rule switch
    {
        "cropAspectFold" => CropAspectFold(a),
        _ => throw new InvalidOperationException($"opRegistry: unknown native rule \"{rule}\""),
    };

    private static OpSchemaException Rethrow(SchemaError err, string prefix) => new(prefix + err.Message);

    // ── public surface ──

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
                v = RunRule(rule, v);
            }
            CheckFields(v, entry.Keys, entry.Holder, null, ["op"]);
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
                throw Bad("\"ask\" must be an object");
            }
            JsonElement schema = registry.GetProperty("ask").GetProperty("schema");
            CheckFields(ask, schema.GetProperty("keys"), schema, new Path("ask.", "", null), []);
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
            CheckValue(v, registry.GetProperty("envelope").GetProperty(key), new Path("", key, null), null);
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
        JsonElement os = registry.GetProperty("opsets").TryGetProperty(opset, out JsonElement o)
            ? o
            : throw new InvalidOperationException($"opRegistry: unknown opset \"{opset}\"");
        if (os.TryGetProperty("overrides", out JsonElement ov) && ov.TryGetProperty(op, out JsonElement over))
        {
            return new OpEntry(op, over, over.GetProperty("keys"), Rules(over), new HashSet<string>(), null, null);
        }
        if (Has(os, "ops", op))
        {
            JsonElement core = registry.GetProperty("ops").EnumerateArray().FirstOrDefault(e => e.GetProperty("id").GetString() == op);
            return core.ValueKind == JsonValueKind.Object ? Resolve(core) : null;
        }
        fail = Has(os, "failOps", op);
        return null;
    }
}
