namespace Stencil.TelegramBot.Domain.Layout;

/// <summary>
/// One polyline / closed shape with its stroke and fill styling.
/// </summary>
/// <remarks>
/// The per-field defaults match every other front-end (browser export,
/// <c>cli/src/layout.zig</c>, <c>mcp/src/layout.rs</c>, <c>pystencil/layout.py</c>):
/// color <c>#FFFF00</c>, thickness <c>2</c>, markerSize <c>4</c>, style <c>solid</c>,
/// locked <c>false</c>, fillColor <c>transparent</c>. JSON keys are camelCase.
/// </remarks>
public sealed record LayoutLine
{
    public const string DefaultColor = "#FFFF00";
    public const double DefaultThickness = 2.0;
    public const double DefaultMarkerSize = 4.0;
    public const string DefaultStyle = "solid";
    public const bool DefaultLocked = false;
    public const string DefaultFillColor = "transparent";

    public IReadOnlyList<LayoutPoint> Points { get; init; } = [];
    public string Color { get; init; } = DefaultColor;
    public double Thickness { get; init; } = DefaultThickness;
    public double MarkerSize { get; init; } = DefaultMarkerSize;
    public string Style { get; init; } = DefaultStyle;
    public bool Locked { get; init; } = DefaultLocked;
    public string FillColor { get; init; } = DefaultFillColor;
}
