using System.Globalization;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// The ISO 216/269 page-format table (A/B/C series, portrait, cm). Hardcoded here because the
/// bot is a thin CLI adapter and never links <c>core/</c> — this mirrors the table in
/// <c>core/page/pageMetrics.cpp</c> (the ISO mm values / 10). Canonical casing is
/// <c>B5</c>-style; canonical order is A0..A10, B0..B10, C0..C10.
/// </summary>
public static class PageFormats
{
    /// <summary>Every named format as (canonical name, portrait width cm, portrait height cm).</summary>
    public static readonly IReadOnlyList<(string Name, double WidthCm, double HeightCm)> All =
    [
        ("A0", 84.1, 118.9), ("A1", 59.4, 84.1), ("A2", 42, 59.4), ("A3", 29.7, 42),
        ("A4", 21, 29.7), ("A5", 14.8, 21), ("A6", 10.5, 14.8), ("A7", 7.4, 10.5),
        ("A8", 5.2, 7.4), ("A9", 3.7, 5.2), ("A10", 2.6, 3.7),
        ("B0", 100, 141.4), ("B1", 70.7, 100), ("B2", 50, 70.7), ("B3", 35.3, 50),
        ("B4", 25, 35.3), ("B5", 17.6, 25), ("B6", 12.5, 17.6), ("B7", 8.8, 12.5),
        ("B8", 6.2, 8.8), ("B9", 4.4, 6.2), ("B10", 3.1, 4.4),
        ("C0", 91.7, 129.7), ("C1", 64.8, 91.7), ("C2", 45.8, 64.8), ("C3", 32.4, 45.8),
        ("C4", 22.9, 32.4), ("C5", 16.2, 22.9), ("C6", 11.4, 16.2), ("C7", 8.1, 11.4),
        ("C8", 5.7, 8.1), ("C9", 4, 5.7), ("C10", 2.8, 4),
    ];

    /// <summary>
    /// Resolve a format name case-insensitively (<c>b5</c> → <c>B5</c>) to its canonical name
    /// and portrait cm dimensions; false for anything unknown (including <c>custom</c>).
    /// </summary>
    public static bool TryGet(string name, out string canonical, out double widthCm, out double heightCm)
    {
        foreach (var (n, w, h) in All)
        {
            if (n.Equals(name, StringComparison.OrdinalIgnoreCase))
            {
                canonical = n;
                widthCm = w;
                heightCm = h;
                return true;
            }
        }
        canonical = "";
        widthCm = heightCm = 0;
        return false;
    }

    /// <summary>A cm value for chat text: at most two decimals, trailing zeros trimmed.</summary>
    public static string Cm(double value) => value.ToString("0.##", CultureInfo.InvariantCulture);
}
