using System.Globalization;

namespace Stencil.TelegramBot.Infrastructure.Configuration;

/// <summary>
/// The environment-reading primitives the configuration records share. A malformed value is
/// never fatal: it falls back to the default so a typo cannot stop the bot from starting.
/// </summary>
internal static class EnvRead
{
    /// <summary>The variable's value, or null when unset/whitespace.</summary>
    public static string? Var(string name) => NullIfBlank(Environment.GetEnvironmentVariable(name));

    /// <summary>Collapse a missing/whitespace value to null.</summary>
    public static string? NullIfBlank(string? value) =>
        string.IsNullOrWhiteSpace(value) ? null : value;

    /// <summary>Parse a positive integer, falling back to <paramref name="fallback"/> when unset or invalid.</summary>
    public static int PositiveInt(string? value, int fallback) =>
        int.TryParse(value, out int parsed) && parsed >= 1 ? parsed : fallback;

    /// <summary>Parse a non-negative integer (0 allowed), falling back when unset or invalid.</summary>
    public static int NonNegativeInt(string? value, int fallback) =>
        int.TryParse(value, out int parsed) && parsed >= 0 ? parsed : fallback;

    /// <summary>Treat <c>1</c>/<c>true</c>/<c>yes</c> (any case, trimmed) as true.</summary>
    public static bool Truthy(string? value) =>
        value is not null && value.Trim().ToLowerInvariant() is "1" or "true" or "yes";

    /// <summary>
    /// Parse a comma/space/semicolon-separated list of Telegram user ids. An unparseable entry is
    /// dropped: a typo must neither widen the list nor stop the bot from starting.
    /// </summary>
    public static IReadOnlySet<long> UserIds(string? value)
    {
        HashSet<long> ids = [];
        if (string.IsNullOrWhiteSpace(value))
        {
            return ids;
        }
        foreach (string part in value.Split([',', ' ', ';', '\t'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            if (long.TryParse(part, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out long id) && id != 0)
            {
                ids.Add(id);
            }
        }
        return ids;
    }
}
