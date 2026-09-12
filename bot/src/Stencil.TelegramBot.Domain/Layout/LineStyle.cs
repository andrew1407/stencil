namespace Stencil.TelegramBot.Domain.Layout;

// Defaults track LayoutLine's, so a freshly drawn line matches every other front-end.
public sealed record LineStyle
{
    public string Color { get; init; } = LayoutLine.DefaultColor;
    public double Thickness { get; init; } = LayoutLine.DefaultThickness;
    public double PointSize { get; init; } = LayoutLine.DefaultPointSize;
    public string Style { get; init; } = LayoutLine.DefaultStyle;

    // Ignored by open polylines.
    public string FillColor { get; init; } = LayoutLine.DefaultFillColor;
}
