using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Domain.Editing;

// Editing intent, not pixels: the bot keeps one base image on disk and re-derives the result
// by replaying this through the CLI pipeline (source -> crop -> rotate -> filter -> layout).
// Crop/rotate are the latest spec, never a baked snapshot, so a render stays reproducible.
public sealed record EditState
{
    // The CLI's grammar, e.g. x1=10% x2=90% y1=10% y2=90%.
    public string? CropSpec { get; init; }

    // On a single-axis crop, derive the missing axis from the page proportion.
    public bool Album { get; init; }

    // Quarter-turns clockwise, normalised to 0..3.
    public int Rotate { get; init; }

    // bw/sepia/invert/contour/none, or a CSS colour / #hex tint.
    public string? Filter { get; init; }

    // A canonical ISO name (B5) or "custom". Rides the saved layout's pageSize; null preserves
    // whatever the fetched layout carried.
    public string? PageFormat { get; init; }

    // In cm, only when PageFormat is "custom".
    public double? CustomPageWidth { get; init; }

    public double? CustomPageHeight { get; init; }

    // Coordinate-transform formulas (x*2+10). Metadata, like the browser's: they never change
    // the raster, but ride the saved layout (formulaX/Y + allowFormulas) for the other surfaces.
    public string? FormulaX { get; init; }

    public string? FormulaY { get; init; }

    public StencilLayout? Layout { get; init; }

    public LineStyle Pen { get; init; } = new();

    public int LineCount => Layout?.Lines.Count ?? 0;

    // A bare original: the pen alone doesn't count as an edit.
    public bool IsEmpty =>
        CropSpec is null && !Album && Rotate == 0 && Filter is null && PageFormat is null && LineCount == 0;
}
