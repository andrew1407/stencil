using System.Text.Json;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Serialization;

namespace Stencil.TelegramBot.Application.Servers;

// Translates a server project's stored layout (the browser's buildLayoutPayload shape) into an
// EditState. The browser rotates then crops in rotated space; the CLI crops then rotates. ReadCrop
// un-rotates cropRect back into original-image space so the CLI yields the same pixels.
public static class ProjectLayoutMapper
{
    public static EditState ToEditState(JsonElement layout, int originalWidth, int originalHeight)
    {
        if (layout.ValueKind != JsonValueKind.Object)
        {
            return new EditState();
        }
        var lines = readLines(layout);
        var rotate = readRotation(layout);
        StencilLayout? drawing = lines.Count == 0
            ? null
            : new StencilLayout
            {
                ImageWidth = readDouble(layout, "imageWidth"),
                ImageHeight = readDouble(layout, "imageHeight"),
                Lines = lines,
            };
        return new EditState
        {
            Layout = drawing,
            Filter = readFilter(layout),
            Rotate = rotate,
            CropSpec = readCrop(layout, rotate, originalWidth, originalHeight),
        };
    }

    private static IReadOnlyList<LayoutLine> readLines(JsonElement layout)
    {
        if (!layout.TryGetProperty("lines", out var lines) || lines.ValueKind != JsonValueKind.Array)
        {
            return [];
        }
        return StencilJson.FromElement<List<LayoutLine>>(lines) ?? [];
    }

    // A custom tint resolves to its filterColor.
    private static string? readFilter(JsonElement layout)
    {
        string? mode = readString(layout, "imageFilter");
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
        if (mode.Equals("invert", StringComparison.OrdinalIgnoreCase))
        {
            return "invert";
        }
        if (mode.Equals("contour", StringComparison.OrdinalIgnoreCase))
        {
            return "contour";
        }
        if (mode.Equals("custom", StringComparison.OrdinalIgnoreCase))
        {
            return readString(layout, "filterColor");
        }
        return null; // "none" or anything else: no filter
    }

    private static int readRotation(JsonElement layout)
    {
        if (!layout.TryGetProperty("rotationQuarters", out var q) || q.ValueKind != JsonValueKind.Number)
        {
            return 0;
        }
        int quarters = (int)Math.Round(q.GetDouble());
        return (((quarters % 4) + 4) % 4);
    }

    // cropRect is in rotated-image space (canonical {x,y,w,h} or legacy {width,height}); null = no
    // crop.
    private static string? readCrop(JsonElement layout, int rotate, int originalWidth, int originalHeight)
    {
        if (originalWidth <= 0 || originalHeight <= 0)
        {
            return null;
        }
        if (!layout.TryGetProperty("cropRect", out var rect) || rect.ValueKind != JsonValueKind.Object)
        {
            return null;
        }
        double rx = readDouble(rect, "x") ?? 0;
        double ry = readDouble(rect, "y") ?? 0;
        // Canonical {w,h} wins; the legacy {width,height} keys still read.
        double rw = readDouble(rect, "w") ?? readDouble(rect, "width") ?? 0;
        double rh = readDouble(rect, "h") ?? readDouble(rect, "height") ?? 0;
        if (rw <= 0 || rh <= 0)
        {
            return null;
        }

        var (ox1, oy1) = unrotatePoint(rx, ry, rotate, originalWidth, originalHeight);
        var (ox2, oy2) = unrotatePoint(rx + rw, ry + rh, rotate, originalWidth, originalHeight);
        int x1 = clamp((int)Math.Round(Math.Min(ox1, ox2)), 0, originalWidth);
        int x2 = clamp((int)Math.Round(Math.Max(ox1, ox2)), 0, originalWidth);
        int y1 = clamp((int)Math.Round(Math.Min(oy1, oy2)), 0, originalHeight);
        int y2 = clamp((int)Math.Round(Math.Max(oy1, oy2)), 0, originalHeight);

        if (x1 <= 0 && y1 <= 0 && x2 >= originalWidth && y2 >= originalHeight)
        {
            return null;
        }
        return $"x1={x1}px x2={x2}px y1={y1}px y2={y2}px";
    }

    private static (double X, double Y) unrotatePoint(double xr, double yr, int rotate, int w, int h) =>
        rotate switch
        {
            1 => (yr, (h - 1) - xr),
            2 => ((w - 1) - xr, (h - 1) - yr),
            3 => ((w - 1) - yr, xr),
            _ => (xr, yr),
        };

    private static int clamp(int value, int min, int max) => Math.Max(min, Math.Min(max, value));

    private static double? readDouble(JsonElement obj, string name) =>
        obj.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Number ? v.GetDouble() : null;

    private static string? readString(JsonElement obj, string name) =>
        obj.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;
}
