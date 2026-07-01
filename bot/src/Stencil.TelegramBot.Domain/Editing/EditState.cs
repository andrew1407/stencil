using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Domain.Editing;

/// <summary>
/// The accumulated, re-applicable editing intent for one working image.
/// </summary>
/// <remarks>
/// The bot keeps a single base image on disk (the <i>original</i>: an uploaded photo, a
/// rendered blank, or a fetched server project's original) and re-derives the result by
/// replaying this state through the CLI pipeline (source → crop → rotate → layout → filter).
/// Mirrors the CLI console's single working image + ordered transforms, except crop/rotate
/// are stored as the latest spec rather than a baked snapshot, so a render is reproducible
/// and the layout JSON is exportable.
/// </remarks>
public sealed record EditState
{
    /// <summary>Crop spec in the CLI's grammar, e.g. <c>x1=10% x2=90% y1=10% y2=90%</c>.</summary>
    public string? CropSpec { get; init; }

    /// <summary>On a single-axis crop, derive the missing axis from the page proportion.</summary>
    public bool Album { get; init; }

    /// <summary>Accumulated quarter-turns clockwise, normalised to <c>0..3</c>.</summary>
    public int Rotate { get; init; }

    /// <summary><c>bw</c>/<c>sepia</c>/<c>none</c> or a CSS colour / <c>#hex</c> tint.</summary>
    public string? Filter { get; init; }

    /// <summary>An applied drawing layout (polylines), or null when none was applied.</summary>
    public StencilLayout? Layout { get; init; }

    /// <summary>The current pen — the style applied to newly drawn lines/shapes.</summary>
    public LineStyle Pen { get; init; } = new();

    /// <summary>The number of drawn lines/shapes in the current layout.</summary>
    public int LineCount => Layout?.Lines.Count ?? 0;

    /// <summary>True when no transforms are pending (a bare original; the pen alone doesn't count).</summary>
    public bool IsEmpty =>
        CropSpec is null && !Album && Rotate == 0 && Filter is null && LineCount == 0;
}
