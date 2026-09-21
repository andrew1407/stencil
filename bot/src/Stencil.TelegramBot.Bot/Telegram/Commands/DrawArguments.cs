using System.Globalization;
using Stencil.TelegramBot.Domain.Layout;

namespace Stencil.TelegramBot.Bot.Telegram.Commands;

// A point token is x,y in image pixels, or x%,y% as a percentage of the working image's size.
public static class DrawArguments
{
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

    public static bool TryParsePoint(string token, double width, double height, out LayoutPoint point)
    {
        point = new LayoutPoint(0, 0);
        string[] parts = token.Split(',');
        if (parts.Length != 2)
        {
            return false;
        }
        if (!tryCoord(parts[0], width, out double x))
        {
            return false;
        }
        if (!tryCoord(parts[1], height, out double y))
        {
            return false;
        }
        point = new LayoutPoint(x, y);
        return true;
    }

    public static IReadOnlyList<LayoutPoint> Rectangle(LayoutPoint a, LayoutPoint b) =>
        new[]
        {
            new LayoutPoint(a.X, a.Y),
            new LayoutPoint(b.X, a.Y),
            new LayoutPoint(b.X, b.Y),
            new LayoutPoint(a.X, b.Y),
        };

    private static bool tryCoord(string raw, double dim, out double value)
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
