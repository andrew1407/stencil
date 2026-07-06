namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>The calendar unit of a parsed expiry duration.</summary>
public enum DurationUnit
{
    Day,
    Week,
    Month,
}

/// <summary>
/// A parsed expiry duration — a positive <see cref="Count"/> of a <see cref="DurationUnit"/>.
/// Resolved against a base instant so months are calendar months (not a fixed 30 days).
/// </summary>
public sealed record ParsedDuration(DurationUnit Unit, int Count)
{
    /// <summary>The absolute instant this duration lands on, measured from <paramref name="baseTime"/>.</summary>
    public DateTimeOffset From(DateTimeOffset baseTime) => Unit switch
    {
        DurationUnit.Day => baseTime.AddDays(Count),
        DurationUnit.Week => baseTime.AddDays(7L * Count),
        DurationUnit.Month => baseTime.AddMonths(Count),
        _ => baseTime,
    };

    /// <summary>A human phrase like "3 days" / "1 week" / "2 months".</summary>
    public override string ToString()
    {
        string unit = Unit switch
        {
            DurationUnit.Day => "day",
            DurationUnit.Week => "week",
            DurationUnit.Month => "month",
            _ => "",
        };
        return $"{Count} {unit}{(Count == 1 ? "" : "s")}";
    }
}

/// <summary>
/// Pure parser for the free-text expiry durations the <c>/expire</c> command accepts — a unit
/// word (singular or plural, or a short form) with an optional count that may lead or trail it:
/// "day", "days 3", "1 week", "week 4", "2 weeks", "fortnight", "3 months", "3d", "1mo". A set of
/// keywords ("never", "forever", …) means "clear the expiry". No Telegram types — unit-testable,
/// like <see cref="CommandParser"/> and <see cref="DrawArguments"/>.
/// </summary>
public static class DurationParser
{
    /// <summary>Reject absurd counts (and keep well clear of <see cref="DateTimeOffset"/> overflow).</summary>
    private const int MaxCount = 1000;

    /// <summary>
    /// Parse <paramref name="text"/>. Returns false when it isn't a recognised duration or clear
    /// keyword. On success either <paramref name="clear"/> is true (drop the expiry, keep forever)
    /// or <paramref name="duration"/> holds a positive unit+count.
    /// </summary>
    public static bool TryParse(string? text, out ParsedDuration duration, out bool clear)
    {
        duration = new ParsedDuration(DurationUnit.Day, 1);
        clear = false;
        string s = (text ?? "").Trim().ToLowerInvariant();
        if (s.Length == 0)
        {
            return false;
        }
        if (s is "never" or "none" or "off" or "forever" or "clear" or "unset" or "keep" or "permanent" or "0")
        {
            clear = true;
            return true;
        }
        string letters = FirstRun(s, char.IsLetter);
        if (!TryMapUnit(letters, out DurationUnit unit, out int multiplier))
        {
            return false;
        }
        string digits = FirstRun(s, char.IsDigit);
        int count = 1;
        if (digits.Length != 0 && (!int.TryParse(digits, out count) || count <= 0 || count > MaxCount))
        {
            return false;
        }
        duration = new ParsedDuration(unit, count * multiplier);
        return true;
    }

    /// <summary>Map a unit word to its unit and a count multiplier (fortnight = 2 weeks).</summary>
    private static bool TryMapUnit(string word, out DurationUnit unit, out int multiplier)
    {
        multiplier = 1;
        switch (word)
        {
            case "d" or "day" or "days":
                unit = DurationUnit.Day;
                return true;
            case "w" or "wk" or "wks" or "week" or "weeks":
                unit = DurationUnit.Week;
                return true;
            case "fortnight" or "fortnights":
                unit = DurationUnit.Week;
                multiplier = 2;
                return true;
            case "mo" or "mon" or "mth" or "mths" or "month" or "months":
                unit = DurationUnit.Month;
                return true;
            default:
                unit = DurationUnit.Day;
                return false;
        }
    }

    /// <summary>The first maximal run of characters matching <paramref name="pred"/>, or "".</summary>
    private static string FirstRun(string s, Func<char, bool> pred)
    {
        int start = -1;
        for (int i = 0; i < s.Length; i++)
        {
            if (pred(s[i]))
            {
                if (start < 0)
                {
                    start = i;
                }
            }
            else if (start >= 0)
            {
                return s[start..i];
            }
        }
        return start < 0 ? "" : s[start..];
    }
}
