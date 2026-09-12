namespace Stencil.TelegramBot.Domain.Layout;

// Every default is pinned by the other front-ends (browser export, layout.zig, layout.rs,
// layout.py).
public sealed record LayoutLine
{
    public const string DEFAULT_COLOR = "#FFFF00";
    public const double DEFAULT_THICKNESS = 2.0;
    public const double DEFAULT_POINT_SIZE = 4.0;
    public const string DEFAULT_STYLE = "solid";
    public const bool DEFAULT_LOCKED = false;
    public const string DEFAULT_FILL_COLOR = "transparent";
    public const string DEFAULT_POINT_COLOR = "";

    public IReadOnlyList<LayoutPoint> Points { get; init; } = [];
    public string Color { get; init; } = DEFAULT_COLOR;
    public double Thickness { get; init; } = DEFAULT_THICKNESS;
    public double PointSize { get; init; } = DEFAULT_POINT_SIZE;
    public string Style { get; init; } = DEFAULT_STYLE;
    public bool Locked { get; init; } = DEFAULT_LOCKED;
    public string FillColor { get; init; } = DEFAULT_FILL_COLOR;

    // Empty means the points inherit Color (core Line::pointColorOr).
    public string PointColor { get; init; } = DEFAULT_POINT_COLOR;
}
