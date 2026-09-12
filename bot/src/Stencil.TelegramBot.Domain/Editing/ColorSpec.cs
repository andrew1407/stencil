using System.Text.Json;

namespace Stencil.TelegramBot.Domain.Editing;

// Whether the CLI's parseColor (core/color/colorNames.cpp) accepts a colour: transparent, #hex
// (3/4/6/8) or a CSS Color Level 4 keyword — a port of mcp's is_color over the embedded canonical
// colorNames.json. The CLI silently SKIPS an unparseable colour, so a forwarded colour must be
// rejected before argv.
public static class ColorSpec
{
    private const string ResourceName = "Stencil.TelegramBot.Domain.Assets.colorNames.json";

    private static readonly Lazy<IReadOnlySet<string>> Names = new(LoadNames);

    public static IReadOnlySet<string> KnownNames => Names.Value;

    public static bool IsValid(string? spec)
    {
        string s = (spec ?? "").Trim().ToLowerInvariant();
        if (s.Length == 0)
        {
            return false;
        }
        if (s == "transparent")
        {
            return true;
        }
        if (s[0] == '#')
        {
            string hex = s[1..];
            return hex.Length is 3 or 4 or 6 or 8 && hex.All(Uri.IsHexDigit);
        }
        return Names.Value.Contains(s);
    }

    private static IReadOnlySet<string> LoadNames()
    {
        using Stream stream = typeof(ColorSpec).Assembly.GetManifestResourceStream(ResourceName)
            ?? throw new InvalidOperationException($"embedded resource {ResourceName} is missing");
        using JsonDocument doc = JsonDocument.Parse(stream);
        HashSet<string> names = new(StringComparer.Ordinal);
        foreach (JsonProperty entry in doc.RootElement.EnumerateObject())
        {
            names.Add(entry.Name.ToLowerInvariant());
        }
        return names;
    }
}
