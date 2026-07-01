using System.Globalization;
using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Pure parsing of <c>/draw</c> point arguments — kept free of Telegram types so it is
/// unit-testable. A point token is <c>x,y</c> in image pixels, or <c>x%,y%</c> as a percentage
/// of the working image's width/height (handy when you don't know the exact size).
/// </summary>
public static class DrawArguments
{
    /// <summary>
    /// Parse every <paramref name="tokens"/> entry into a point (percentages resolved against
    /// <paramref name="width"/>/<paramref name="height"/>). Returns false with a friendly
    /// <paramref name="error"/> on the first bad token or when no points are given.
    /// </summary>
    public static bool TryParsePoints(
        IReadOnlyList<string> tokens,
        double width,
        double height,
        out List<LayoutPoint> points,
        out string? error)
    {
        points = new List<LayoutPoint>();
        error = null;
        foreach (string token in tokens)
        {
            if (!TryParsePoint(token, width, height, out LayoutPoint point))
            {
                error = $"Bad point '{token}'. Use x,y (pixels) or x%,y%.";
                points = new List<LayoutPoint>();
                return false;
            }
            points.Add(point);
        }
        if (points.Count == 0)
        {
            error = "No points given.";
            return false;
        }
        return true;
    }

    /// <summary>Parse a single <c>x,y</c> (or <c>x%,y%</c>) token.</summary>
    public static bool TryParsePoint(string token, double width, double height, out LayoutPoint point)
    {
        point = new LayoutPoint(0, 0);
        string[] parts = token.Split(',');
        if (parts.Length != 2)
        {
            return false;
        }
        if (!TryCoord(parts[0], width, out double x))
        {
            return false;
        }
        if (!TryCoord(parts[1], height, out double y))
        {
            return false;
        }
        point = new LayoutPoint(x, y);
        return true;
    }

    /// <summary>The four corners of the rectangle spanned by two opposite corners.</summary>
    public static IReadOnlyList<LayoutPoint> Rectangle(LayoutPoint a, LayoutPoint b) =>
        new[]
        {
            new LayoutPoint(a.X, a.Y),
            new LayoutPoint(b.X, a.Y),
            new LayoutPoint(b.X, b.Y),
            new LayoutPoint(a.X, b.Y),
        };

    /// <summary>Parse one coordinate: a bare pixel value, or a <c>%</c> of <paramref name="dim"/>.</summary>
    private static bool TryCoord(string raw, double dim, out double value)
    {
        value = 0;
        string s = raw.Trim();
        if (s.Length == 0)
        {
            return false;
        }
        bool percent = s.EndsWith('%');
        if (percent)
        {
            s = s[..^1];
        }
        if (!double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out double n))
        {
            return false;
        }
        value = percent ? n / 100.0 * dim : n;
        return true;
    }
}
