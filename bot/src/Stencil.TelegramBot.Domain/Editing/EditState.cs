using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Domain.Editing;

// Editing intent, not pixels: one base image on disk, the result re-derived through the CLI pipeline.
// Crop/rotate are the latest spec, never a baked snapshot, so a render stays reproducible.
public sealed record EditState
{
    public string? CropSpec { get; init; }

    // On a single-axis crop, derive the missing axis from the page proportion.
    public bool Album { get; init; }

    // Quarter-turns clockwise, normalised to 0..3.
    public int Rotate { get; init; }

    // bw/sepia/invert/contour/none, or a CSS colour / #hex tint.
    public string? Filter { get; init; }

    // A canonical ISO name (B5) or "custom"; null preserves whatever the fetched layout carried.
    public string? PageFormat { get; init; }

    // In cm, only when PageFormat is "custom".
    public double? CustomPageWidth { get; init; }

    public double? CustomPageHeight { get; init; }

    // Metadata, like the browser's: never changes the raster, rides the saved layout for other
    // surfaces.
    public string? FormulaX { get; init; }

    public string? FormulaY { get; init; }

    public StencilLayout? Layout { get; init; }

    public LineStyle Pen { get; init; } = new();

    public int LineCount => Layout?.Lines.Count ?? 0;

    // A bare original: the pen alone doesn't count as an edit.
    public bool IsEmpty =>
        CropSpec is null && !Album && Rotate == 0 && Filter is null && PageFormat is null && LineCount == 0;
}
