using System.Globalization;
using System.Text.Json;
using static Stencil.TelegramBot.Application.Llm.Schema.SchemaJson;
using static Stencil.TelegramBot.Application.Llm.Schema.SchemaError;
using static Stencil.TelegramBot.Application.Llm.Schema.SchemaPath;

namespace Stencil.TelegramBot.Application.Llm.Schema;

// The cross-field half of the registry's rules; values go back through KeySpecChecker.
internal static class PresenceRules
{
    // skip names keys that are neither declared nor unknown (the action's own "op").
    public static void CheckFields(
        KeySpecChecker checker, JsonElement obj, JsonElement fields, JsonElement holder, SchemaPath? path, string[] skip)
    {
        // An object spec with allowUnknown (the envelope's variant objects) tolerates undeclared
        // keys.
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
        checkGroups(obj, holder, declared);
        foreach (JsonProperty field in fields.EnumerateObject())
        {
            string k = field.Name;
            JsonElement spec = field.Value;
            SchemaPath at = Child(path, k);
            if (!IsPresent(obj, k))
            {
                checkMissing(obj, spec, at);
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
            checker.CheckValue(obj.GetProperty(k), spec, at, obj);
        }
    }

    private static void checkGroups(JsonElement obj, JsonElement holder, List<string> declared)
    {
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
    }

    private static void checkMissing(JsonElement obj, JsonElement spec, SchemaPath at)
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
    }
}
