using System.Globalization;
using System.Text.Json;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// The ISO 216/269 page-format table (A/B/C series, portrait, cm), parsed once from the
/// canonical <c>PAGE_SIZES</c> in <c>browser/js/config/constants.json</c>, embedded into this
/// assembly at build time (the bot is a thin CLI adapter and never links <c>core/</c>).
/// Canonical casing is <c>B5</c>-style; canonical order is the JSON's own: A0..A10, B0..B10, C0..C10.
/// </summary>
public static class PageFormats
{
    private const string ResourceName = "Stencil.TelegramBot.Bot.Assets.constants.json";

    private static readonly Lazy<IReadOnlyList<(string Name, double WidthCm, double HeightCm)>> Table =
        new(LoadTable);

    /// <summary>Every named format as (canonical name, portrait width cm, portrait height cm).</summary>
    public static IReadOnlyList<(string Name, double WidthCm, double HeightCm)> All => Table.Value;

    private static IReadOnlyList<(string, double, double)> LoadTable()
    {
        using Stream stream = typeof(PageFormats).Assembly.GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException($"embedded resource {ResourceName} is missing");
        using JsonDocument doc = JsonDocument.Parse(stream);
        var table = new List<(string, double, double)>();
        foreach (JsonProperty size in doc.RootElement.GetProperty("PAGE_SIZES").EnumerateObject())
        {
            table.Add((size.Name,
                size.Value.GetProperty("width").GetDouble(),
                size.Value.GetProperty("height").GetDouble()));
        }
        return table;
    }

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
