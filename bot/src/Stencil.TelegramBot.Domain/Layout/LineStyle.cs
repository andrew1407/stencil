namespace Stencil.TelegramBot.Domain.Layout;

// Defaults track LayoutLine's, so a freshly drawn line matches every other front-end.
public sealed record LineStyle
{
    public string Color { get; init; } = LayoutLine.DEFAULT_COLOR;
    public double Thickness { get; init; } = LayoutLine.DEFAULT_THICKNESS;
    public double PointSize { get; init; } = LayoutLine.DEFAULT_POINT_SIZE;
    public string Style { get; init; } = LayoutLine.DEFAULT_STYLE;

    // Ignored by open polylines.
    public string FillColor { get; init; } = LayoutLine.DEFAULT_FILL_COLOR;
}
