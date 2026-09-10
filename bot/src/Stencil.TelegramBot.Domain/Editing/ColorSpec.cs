using System.Text.Json;

namespace Stencil.TelegramBot.Domain.Editing;

/// <summary>
/// Whether a colour string is one the CLI's <c>parseColor</c> accepts (<c>cli/src/core.zig</c> →
/// <c>parseColor</c> in <c>core/color/colorNames.cpp</c>): <c>transparent</c>, <c>#</c> + 3/4/6/8
/// hex digits, or a CSS Color Level 4 keyword. A port of <c>is_color</c> in
/// <c>mcp/src/args.rs</c>, over the same canonical <c>browser/js/config/colorNames.json</c>
/// embedded here at build time.
/// </summary>
/// <remarks>
/// The CLI silently SKIPS an unparseable colour — a <c>--blank</c> would come out white, a pen
/// stroke black — so an adapter that forwards a model- or user-chosen colour must reject it
/// before it reaches argv or the layout.
/// </remarks>
public static class ColorSpec
{
    private const string ResourceName = "Stencil.TelegramBot.Domain.Assets.colorNames.json";

    private static readonly Lazy<IReadOnlySet<string>> Names = new(LoadNames);

    /// <summary>The CSS keywords the core recognises, lowercased (148 of them).</summary>
    public static IReadOnlySet<string> KnownNames => Names.Value;

    /// <summary>Whether <paramref name="spec"/> parses, after trimming and ASCII-lowercasing.</summary>
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
