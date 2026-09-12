using System.Globalization;

namespace Stencil.TelegramBot.Infrastructure.Configuration;

// A malformed value is never fatal: it falls back to the default so a typo cannot stop the bot from
// starting.
internal static class EnvRead
{
    public static string? Var(string name) => NullIfBlank(Environment.GetEnvironmentVariable(name));

    public static string? NullIfBlank(string? value) =>
        string.IsNullOrWhiteSpace(value) ? null : value;

    public static int PositiveInt(string? value, int fallback) =>
        int.TryParse(value, out int parsed) && parsed >= 1 ? parsed : fallback;

    public static int NonNegativeInt(string? value, int fallback) =>
        int.TryParse(value, out int parsed) && parsed >= 0 ? parsed : fallback;

    // 1/true/yes, any case, trimmed.
    public static bool Truthy(string? value) =>
        value is not null && value.Trim().ToLowerInvariant() is "1" or "true" or "yes";

    // An unparseable entry is dropped: a typo must neither widen the list nor stop the bot from
    // starting.
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
