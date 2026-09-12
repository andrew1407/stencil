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

    private static IReadOnlyList<LayoutLine> ReadLines(JsonElement layout)
    {
        if (!layout.TryGetProperty("lines", out var lines) || lines.ValueKind != JsonValueKind.Array)
        {
            return [];
        }
        return StencilJson.FromElement<List<LayoutLine>>(lines) ?? [];
    }

    // A custom tint resolves to its filterColor.
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
            return ReadString(layout, "filterColor");
        }
        return null; // "none" or anything else: no filter
    }

    private static int ReadRotation(JsonElement layout)
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
        // Canonical {w,h} wins; the legacy {width,height} keys still read.
        double rw = ReadDouble(rect, "w") ?? ReadDouble(rect, "width") ?? 0;
        double rh = ReadDouble(rect, "h") ?? ReadDouble(rect, "height") ?? 0;
        if (rw <= 0 || rh <= 0)
        {
            return null;
        }

        var (ox1, oy1) = UnrotatePoint(rx, ry, rotate, originalWidth, originalHeight);
        var (ox2, oy2) = UnrotatePoint(rx + rw, ry + rh, rotate, originalWidth, originalHeight);
        int x1 = Clamp((int)Math.Round(Math.Min(ox1, ox2)), 0, originalWidth);
        int x2 = Clamp((int)Math.Round(Math.Max(ox1, ox2)), 0, originalWidth);
        int y1 = Clamp((int)Math.Round(Math.Min(oy1, oy2)), 0, originalHeight);
        int y2 = Clamp((int)Math.Round(Math.Max(oy1, oy2)), 0, originalHeight);

        if (x1 <= 0 && y1 <= 0 && x2 >= originalWidth && y2 >= originalHeight)
        {
            return null;
        }
        return $"x1={x1}px x2={x2}px y1={y1}px y2={y2}px";
    }

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
