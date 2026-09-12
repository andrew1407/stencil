using System.Text.Json.Serialization;

namespace Stencil.TelegramBot.Domain.Layout;

// The JSON the CLI's --layout consumes; coordinates are image pixels, ImageWidth/Height advisory.
public sealed record StencilLayout
{
    public double? ImageWidth { get; init; }
    public double? ImageHeight { get; init; }

    [JsonPropertyName("imageFilter")]
    public string? Filter { get; init; }

    // Legacy wire key, read-only: the null getter never serializes, canonical wins when both come
    // in.
    [JsonPropertyName("filter")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? LegacyFilter { get => null; init { Filter ??= value; } }

    public IReadOnlyList<LayoutLine> Lines { get; init; } = [];
}
