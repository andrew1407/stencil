using System.Text.Json;
using static Stencil.TelegramBot.Application.Llm.SchemaJson;
using static Stencil.TelegramBot.Application.Llm.SchemaError;

namespace Stencil.TelegramBot.Application.Llm;

// The cross-field rules the table cannot express; each rewrites the action, and the rewritten copy
// is validated.
internal static class NativeRules
{
    public static JsonElement Run(string rule, JsonElement a) => rule switch
    {
        "cropAspectFold" => cropAspectFold(a),
        _ => throw new InvalidOperationException($"opRegistry: unknown native rule \"{rule}\""),
    };

    // §3.2 tolerance: "aspect" beside "spec" folds into the spec when it lacks one; a conflicting
    // duplicate fails.
    private static JsonElement cropAspectFold(JsonElement a)
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
}
