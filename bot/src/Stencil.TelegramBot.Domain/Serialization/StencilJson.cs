using System.Text.Json;
using System.Text.Json.Serialization;

namespace Stencil.TelegramBot.Domain.Serialization;

// The one place the bot's JSON conventions live. camelCase matches the wire shapes every other
// front-end uses (protocol DTOs, the layout schema): ImageWidth -> imageWidth, ImageW -> imageW.
public static class StencilJson
{
    // camelCase, omit null on write, case-insensitive on read.
    public static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        PropertyNameCaseInsensitive = true,
    };

    // Same conventions, pretty-printed: human-facing JSON downloads.
    public static readonly JsonSerializerOptions Indented = new(Options)
    {
        WriteIndented = true,
    };

    public static string Serialize<T>(T value) => JsonSerializer.Serialize(value, Options);

    public static string SerializeIndented<T>(T value) => JsonSerializer.Serialize(value, Indented);

    // A detached element, e.g. a layout payload.
    public static JsonElement ToElement<T>(T value) =>
        JsonSerializer.SerializeToElement(value, Options);

    public static T? FromElement<T>(JsonElement element) =>
        element.Deserialize<T>(Options);
}
