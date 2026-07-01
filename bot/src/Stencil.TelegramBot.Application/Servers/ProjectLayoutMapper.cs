using System.Text.Json;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Application.Servers;

/// <summary>
/// Translates a server project's stored layout (the browser's <c>buildLayoutPayload</c> shape:
/// <c>lines</c>, <c>imageFilter</c>/<c>filterColor</c>, <c>rotationQuarters</c>, <c>cropRect</c>)
/// into the bot's <see cref="EditState"/>, so re-rendering the original reproduces the same
/// result every other front-end shows — instead of the bare original.
/// </summary>
/// <remarks>
/// Pure (no I/O), so it is unit-tested against real project layouts. The key subtlety: the
/// browser rotates the original and then crops in that <i>rotated</i> space, whereas the CLI
/// pipeline crops then rotates. Cropping commutes with rotation when the crop rectangle is
/// transformed, so <see cref="ReadCrop"/> un-rotates <c>cropRect</c> back into original-image
/// pixel space; the CLI then crops the original and rotates, yielding the same pixels.
/// </remarks>
public static class ProjectLayoutMapper
{
    /// <summary>Map a project layout to an edit state, given the original image dimensions.</summary>
    public static EditState ToEditState(JsonElement layout, int originalWidth, int originalHeight)
    {
        if (layout.ValueKind != JsonValueKind.Object)
        {
            return new EditState();
        }
        var lines = ReadLines(layout);
        var rotate = ReadRotation(layout);
        StencilLayout? drawing = lines.Count == 0
            ? null
            : new StencilLayout
            {
                ImageWidth = ReadDouble(layout, "imageWidth"),
                ImageHeight = ReadDouble(layout, "imageHeight"),
                Lines = lines,
            };
        return new EditState
        {
            Layout = drawing,
            Filter = ReadFilter(layout),
            Rotate = rotate,
            CropSpec = ReadCrop(layout, rotate, originalWidth, originalHeight),
        };
    }

    /// <summary>Deserialize the <c>lines</c> array into the shared line model (defaults fill gaps).</summary>
    private static IReadOnlyList<LayoutLine> ReadLines(JsonElement layout)
    {
        if (!layout.TryGetProperty("lines", out var lines) || lines.ValueKind != JsonValueKind.Array)
        {
            return [];
        }
        return StencilJson.FromElement<List<LayoutLine>>(lines) ?? [];
    }

    /// <summary>
    /// Map <c>imageFilter</c> (<c>none</c>/<c>bw</c>/<c>sepia</c>/<c>custom</c>) to the CLI filter
    /// argument; a <c>custom</c> tint resolves to its <c>filterColor</c>.
    /// </summary>
    private static string? ReadFilter(JsonElement layout)
    {
        string? mode = ReadString(layout, "imageFilter");
        if (mode is null)
        {
            return null;
        }
        if (mode.Equals("bw", StringComparison.OrdinalIgnoreCase))
        {
            return "bw";
        }
        if (mode.Equals("sepia", StringComparison.OrdinalIgnoreCase))
        {
            return "sepia";
        }
        if (mode.Equals("custom", StringComparison.OrdinalIgnoreCase))
        {
            return ReadString(layout, "filterColor");
        }
        return null; // "none" or anything else: no filter
    }

    /// <summary>Read <c>rotationQuarters</c> normalised to 0..3.</summary>
    private static int ReadRotation(JsonElement layout)
    {
        if (!layout.TryGetProperty("rotationQuarters", out var q) || q.ValueKind != JsonValueKind.Number)
        {
            return 0;
        }
        int quarters = (int)Math.Round(q.GetDouble());
        return (((quarters % 4) + 4) % 4);
    }

    /// <summary>
    /// Read <c>cropRect</c> (in rotated-image space) and return a CLI crop spec in original-image
    /// pixels, or null when there is no crop (the rect covers the whole original).
    /// </summary>
    private static string? ReadCrop(JsonElement layout, int rotate, int originalWidth, int originalHeight)
    {
        if (originalWidth <= 0 || originalHeight <= 0)
        {
            return null;
        }
        if (!layout.TryGetProperty("cropRect", out var rect) || rect.ValueKind != JsonValueKind.Object)
        {
            return null;
        }
        double rx = ReadDouble(rect, "x") ?? 0;
        double ry = ReadDouble(rect, "y") ?? 0;
        double rw = ReadDouble(rect, "width") ?? 0;
        double rh = ReadDouble(rect, "height") ?? 0;
        if (rw <= 0 || rh <= 0)
        {
            return null;
        }

        // Un-rotate the two opposite corners from rotated space back into original-image space.
        var (ox1, oy1) = UnrotatePoint(rx, ry, rotate, originalWidth, originalHeight);
        var (ox2, oy2) = UnrotatePoint(rx + rw, ry + rh, rotate, originalWidth, originalHeight);
        int x1 = Clamp((int)Math.Round(Math.Min(ox1, ox2)), 0, originalWidth);
        int x2 = Clamp((int)Math.Round(Math.Max(ox1, ox2)), 0, originalWidth);
        int y1 = Clamp((int)Math.Round(Math.Min(oy1, oy2)), 0, originalHeight);
        int y2 = Clamp((int)Math.Round(Math.Max(oy1, oy2)), 0, originalHeight);

        // No-op crop (covers the whole original): skip it.
        if (x1 <= 0 && y1 <= 0 && x2 >= originalWidth && y2 >= originalHeight)
        {
            return null;
        }
        return $"x1={x1}px x2={x2}px y1={y1}px y2={y2}px";
    }

    /// <summary>
    /// Map a point in the rotated image (original rotated <paramref name="rotate"/> quarters
    /// clockwise) back to original-image coordinates.
    /// </summary>
    private static (double X, double Y) UnrotatePoint(double xr, double yr, int rotate, int w, int h) =>
        rotate switch
        {
            1 => (yr, (h - 1) - xr),
            2 => ((w - 1) - xr, (h - 1) - yr),
            3 => ((w - 1) - yr, xr),
            _ => (xr, yr),
        };

    private static int Clamp(int value, int min, int max) => Math.Max(min, Math.Min(max, value));

    private static double? ReadDouble(JsonElement obj, string name) =>
        obj.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Number ? v.GetDouble() : null;

    private static string? ReadString(JsonElement obj, string name) =>
        obj.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;
}
