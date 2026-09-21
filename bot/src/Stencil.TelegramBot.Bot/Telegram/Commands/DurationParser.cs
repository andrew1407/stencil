namespace Stencil.TelegramBot.Bot.Telegram.Commands;

public enum DurationUnit
{
    DAY,
    WEEK,
    MONTH,
}

// Resolved against a base instant so months are calendar months (not a fixed 30 days).
public sealed record ParsedDuration(DurationUnit Unit, int Count)
{
    public DateTimeOffset From(DateTimeOffset baseTime) => Unit switch
    {
        DurationUnit.DAY => baseTime.AddDays(Count),
        DurationUnit.WEEK => baseTime.AddDays(7L * Count),
        DurationUnit.MONTH => baseTime.AddMonths(Count),
        _ => baseTime,
    };

    private static readonly Dictionary<DurationUnit, string> _unitNames = new()
    {
        [DurationUnit.DAY] = "day", [DurationUnit.WEEK] = "week", [DurationUnit.MONTH] = "month",
    };

    public override string ToString()
    {
        string unit = _unitNames.GetValueOrDefault(Unit, "");
        return $"{Count} {unit}{(Count == 1 ? "" : "s")}";
    }
}

// A unit word (singular, plural or short form) with an optional count that may lead or trail it:
// "day", "days 3", "1 week", "fortnight", "3d", "1mo"; "never"/"forever" mean "clear the expiry".
public static class DurationParser
{
    // Keeps well clear of DateTimeOffset overflow.
    private const int _maxCount = 1000;

    public static bool TryParse(string? text, out ParsedDuration duration, out bool clear)
    {
        duration = new ParsedDuration(DurationUnit.DAY, 1);
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
        string letters = firstRun(s, char.IsLetter);
        if (!tryMapUnit(letters, out DurationUnit unit, out int multiplier))
        {
            return false;
        }
        string digits = firstRun(s, char.IsDigit);
        int count = 1;
        if (digits.Length != 0 && (!int.TryParse(digits, out count) || count <= 0 || count > _maxCount))
        {
            return false;
        }
        duration = new ParsedDuration(unit, count * multiplier);
        return true;
    }

    // fortnight = 2 weeks.
    private static readonly Dictionary<string, (DurationUnit Unit, int Multiplier)> _units = new(StringComparer.Ordinal)
    {
        ["d"] = (DurationUnit.DAY, 1), ["day"] = (DurationUnit.DAY, 1), ["days"] = (DurationUnit.DAY, 1),
        ["w"] = (DurationUnit.WEEK, 1), ["wk"] = (DurationUnit.WEEK, 1), ["wks"] = (DurationUnit.WEEK, 1),
        ["week"] = (DurationUnit.WEEK, 1), ["weeks"] = (DurationUnit.WEEK, 1),
        ["fortnight"] = (DurationUnit.WEEK, 2), ["fortnights"] = (DurationUnit.WEEK, 2),
        ["mo"] = (DurationUnit.MONTH, 1), ["mon"] = (DurationUnit.MONTH, 1), ["mth"] = (DurationUnit.MONTH, 1),
        ["mths"] = (DurationUnit.MONTH, 1), ["month"] = (DurationUnit.MONTH, 1), ["months"] = (DurationUnit.MONTH, 1),
    };

    private static bool tryMapUnit(string word, out DurationUnit unit, out int multiplier)
    {
        bool known = _units.TryGetValue(word, out (DurationUnit Unit, int Multiplier) mapped);
        (unit, multiplier) = known ? mapped : (DurationUnit.DAY, 1);
        return known;
    }

    private static string firstRun(string s, Func<char, bool> pred)
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
