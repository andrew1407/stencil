using System.Text.Json;

namespace Stencil.TelegramBot.Application.Llm;

internal static class SchemaJson
{
    public static readonly JsonElement EmptyObject = JsonDocument.Parse("{}").RootElement.Clone();

    /// <summary>A key is "present" when it exists and is not null.</summary>
    public static bool IsPresent(JsonElement obj, string key) =>
        obj.TryGetProperty(key, out JsonElement v) && v.ValueKind != JsonValueKind.Null;

    public static bool IsFiniteNumber(JsonElement v, out double d)
    {
        d = 0;
        return v.ValueKind == JsonValueKind.Number && v.TryGetDouble(out d) && double.IsFinite(d);
    }

    public static bool JsonEquals(JsonElement a, JsonElement b) => a.ValueKind switch
    {
        JsonValueKind.String => b.ValueKind == JsonValueKind.String && a.GetString() == b.GetString(),
        JsonValueKind.Number => b.ValueKind == JsonValueKind.Number && a.GetDouble() == b.GetDouble(),
        JsonValueKind.True or JsonValueKind.False => a.ValueKind == b.ValueKind,
        _ => false,
    };

    public static bool Contains(JsonElement list, JsonElement v) => list.EnumerateArray().Any(x => JsonEquals(x, v));

    public static bool Flag(JsonElement spec, string name) =>
        spec.TryGetProperty(name, out JsonElement f) && f.ValueKind == JsonValueKind.True;

    public static string QuoteList(JsonElement list) =>
        string.Join(", ", list.EnumerateArray().Select(static x => x.ValueKind == JsonValueKind.String ? $"\"{x.GetString()}\"" : x.GetRawText()));

    public static JsonElement Single(JsonElement v)
    {
        using JsonDocument doc = JsonDocument.Parse($"[{v.GetRawText()}]");
        return doc.RootElement.Clone();
    }
}
