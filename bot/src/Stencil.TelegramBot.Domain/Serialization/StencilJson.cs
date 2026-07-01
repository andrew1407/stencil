using System.Text.Json;
using System.Text.Json.Serialization;

namespace Stencil.TelegramBot.Domain.Serialization;

/// <summary>
/// The one place the bot's JSON conventions live, shared by the server REST client, the
/// layout import/export, and the session store. camelCase property names match the wire
/// shapes every other Stencil front-end uses (protocol DTOs and the layout schema), so
/// e.g. <c>ImageWidth → imageWidth</c>, <c>MarkerSize → markerSize</c>, <c>ImageW → imageW</c>.
/// </summary>
public static class StencilJson
{
    /// <summary>Compact options: camelCase, omit null on write, case-insensitive on read.</summary>
    public static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        PropertyNameCaseInsensitive = true,
    };

    /// <summary>Same conventions, pretty-printed — used for human-facing JSON downloads.</summary>
    public static readonly JsonSerializerOptions Indented = new(Options)
    {
        WriteIndented = true,
    };

    /// <summary>Serialize a value to its camelCase JSON string.</summary>
    public static string Serialize<T>(T value) => JsonSerializer.Serialize(value, Options);

    /// <summary>Serialize a value to a pretty camelCase JSON string (for downloads).</summary>
    public static string SerializeIndented<T>(T value) => JsonSerializer.Serialize(value, Indented);

    /// <summary>Capture a value as a detached <see cref="JsonElement"/> (e.g. a layout payload).</summary>
    public static JsonElement ToElement<T>(T value) =>
        JsonSerializer.SerializeToElement(value, Options);

    /// <summary>Parse a <see cref="JsonElement"/> back into <typeparamref name="T"/>.</summary>
    public static T? FromElement<T>(JsonElement element) =>
        element.Deserialize<T>(Options);
}
