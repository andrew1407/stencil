using System.Globalization;
using System.Text.Json;

namespace Stencil.TelegramBot.Bot.Telegram;

// The ISO 216/269 table, parsed once from the canonical PAGE_SIZES in
// browser/js/config/constants.json (embedded; the bot never links core/). Canonical casing is
// B5-style; canonical order is the JSON's own.
public static class PageFormats
{
    private const string _resourceName = "Stencil.TelegramBot.Bot.Assets.constants.json";

    private static readonly Lazy<IReadOnlyList<(string Name, double WidthCm, double HeightCm)>> _table =
        new(loadTable);

    public static IReadOnlyList<(string Name, double WidthCm, double HeightCm)> All => _table.Value;

    private static IReadOnlyList<(string, double, double)> loadTable()
    {
        using Stream stream = typeof(PageFormats).Assembly.GetManifestResourceStream(_resourceName)
            ?? throw new InvalidOperationException($"embedded resource {_resourceName} is missing");
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

    // Case-insensitive (b5 → B5); false for anything unknown, including custom.
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

    // At most two decimals, trailing zeros trimmed.
    public static string Cm(double value) => value.ToString("0.##", CultureInfo.InvariantCulture);
}
