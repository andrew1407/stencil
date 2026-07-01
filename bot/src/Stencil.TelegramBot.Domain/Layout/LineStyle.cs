namespace Stencil.TelegramBot.Domain.Layout;

/// <summary>
/// The current "pen" — the styling applied to newly drawn lines/shapes until changed. Holds
/// the same style fields as <see cref="LayoutLine"/> (minus the points), with the identical
/// per-line defaults so a freshly drawn line matches every other front-end's defaults.
/// </summary>
public sealed record LineStyle
{
    public string Color { get; init; } = LayoutLine.DefaultColor;
    public double Thickness { get; init; } = LayoutLine.DefaultThickness;
    public double MarkerSize { get; init; } = LayoutLine.DefaultMarkerSize;
    public string Style { get; init; } = LayoutLine.DefaultStyle;

    /// <summary>Fill for a closed shape, or <c>transparent</c>. Ignored by open polylines.</summary>
    public string FillColor { get; init; } = LayoutLine.DefaultFillColor;
}
