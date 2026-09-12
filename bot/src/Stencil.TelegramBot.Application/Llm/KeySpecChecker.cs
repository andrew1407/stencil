using System.Text.Json;
using System.Text.RegularExpressions;
using static Stencil.TelegramBot.Application.Llm.SchemaJson;
using static Stencil.TelegramBot.Application.Llm.SchemaError;
using static Stencil.TelegramBot.Application.Llm.SchemaPath;

namespace Stencil.TelegramBot.Application.Llm;

// The per-VALUE half of the registry's rules; cross-field presence is PresenceRules' job.
internal sealed class KeySpecChecker
{
    private readonly JsonElement limits;
    private readonly JsonElement describe;
    private readonly IReadOnlyDictionary<string, Regex> regexes;

    public KeySpecChecker(SchemaLoader loader)
    {
        limits = loader.Limits;
        describe = loader.Describe;
        regexes = loader.Regexes;
    }

    public int Limit(JsonElement v) =>
        v.ValueKind == JsonValueKind.Number ? (int)v.GetDouble() : Limit(v.GetString()!);

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

    private string describeRegex(string name) =>
        describe.TryGetProperty(name, out JsonElement d) ? d.GetString()! : name;

    public void CheckValue(JsonElement v, JsonElement spec, SchemaPath path, JsonElement? parent)
    {
        switch (spec.GetProperty("type").GetString())
        {
            case "string": checkString(v, spec, path, parent); break;
            case "integer":
            case "number": checkNumber(v, spec, path); break;
            case "boolean": checkBoolean(v, spec, path); break;
            case "array": checkArray(v, spec, path); break;
            case "object": checkObject(v, spec, path); break;
            default: throw new InvalidOperationException($"opRegistry: unknown type \"{spec.GetProperty("type")}\"");
        }
    }

    private void checkString(JsonElement v, JsonElement spec, SchemaPath path, JsonElement? parent)
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
            throw Bad($"{Label(path)} must be {string.Join(" or ", names.Select(describeRegex))}");
        }
        if (spec.TryGetProperty("regexNot", out JsonElement not) && regexes[not.GetString()!].IsMatch(s))
        {
            throw Bad($"{Label(path)} must be a local value, not {describeRegex(not.GetString()!)}");
        }
    }

    private static void checkNumber(JsonElement v, JsonElement spec, SchemaPath path)
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

    private static void checkBoolean(JsonElement v, JsonElement spec, SchemaPath path)
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

    private void checkArray(JsonElement v, JsonElement spec, SchemaPath path)
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

    private void checkObject(JsonElement v, JsonElement spec, SchemaPath path)
    {
        if (v.ValueKind != JsonValueKind.Object)
        {
            throw Bad($"{Label(path)} must be an object");
        }
        bool hasFields = spec.TryGetProperty("fields", out JsonElement fields);
        if (hasFields || spec.TryGetProperty("minFields", out _))
        {
            PresenceRules.CheckFields(this, v, hasFields ? fields : EmptyObject, spec, path, []);
        }
    }
}
