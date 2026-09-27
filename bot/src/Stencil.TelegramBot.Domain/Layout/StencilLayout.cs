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

    // A crop from an oldW x oldH view to a newW x newH window (core::cropChange): an album/portrait
    // flip clears the lines, else every point scales by the width ratio (width > height is album).
    public StencilLayout Recropped(int oldW, int oldH, int newW, int newH)
    {
        if ((oldW > oldH) != (newW > newH))
        {
            return this with { Lines = [] };
        }
        if (oldW <= 0 || newW == oldW)
        {
            return this;
        }
        double scale = (double)newW / oldW;
        return this with
        {
            Lines = [.. Lines.Select(line => line with
            {
                Points = [.. line.Points.Select(p => new LayoutPoint(p.X * scale, p.Y * scale))],
            })],
        };
    }
}
