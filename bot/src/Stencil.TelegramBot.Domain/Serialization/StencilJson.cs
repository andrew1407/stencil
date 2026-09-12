using System.Text.Json;
using System.Text.Json.Serialization;

namespace Stencil.TelegramBot.Domain.Serialization;

// camelCase matches the wire shapes every other front-end uses (protocol DTOs, the layout schema).
public static class StencilJson
{
    public static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        PropertyNameCaseInsensitive = true,
    };

    public static readonly JsonSerializerOptions Indented = new(Options)
    {
        WriteIndented = true,
    };

    public static string Serialize<T>(T value) => JsonSerializer.Serialize(value, Options);

    public static string SerializeIndented<T>(T value) => JsonSerializer.Serialize(value, Indented);

    public static JsonElement ToElement<T>(T value) =>
        JsonSerializer.SerializeToElement(value, Options);

    public static T? FromElement<T>(JsonElement element) =>
        element.Deserialize<T>(Options);
}
