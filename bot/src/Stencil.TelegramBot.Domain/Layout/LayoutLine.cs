namespace Stencil.TelegramBot.Domain.Layout;

// Every default below is pinned by the other front-ends (browser export, cli/src/layout.zig,
// mcp/src/layout.rs, pystencil/layout.py). JSON keys are camelCase.
public sealed record LayoutLine
{
    public const string DefaultColor = "#FFFF00";
    public const double DefaultThickness = 2.0;
    public const double DefaultPointSize = 4.0;
    public const string DefaultStyle = "solid";
    public const bool DefaultLocked = false;
    public const string DefaultFillColor = "transparent";
    public const string DefaultPointColor = "";

    public IReadOnlyList<LayoutPoint> Points { get; init; } = [];
    public string Color { get; init; } = DefaultColor;
    public double Thickness { get; init; } = DefaultThickness;
    public double PointSize { get; init; } = DefaultPointSize;
    public string Style { get; init; } = DefaultStyle;
    public bool Locked { get; init; } = DefaultLocked;
    public string FillColor { get; init; } = DefaultFillColor;

    // Empty means the points inherit Color (core Line::pointColorOr).
    public string PointColor { get; init; } = DefaultPointColor;
}
