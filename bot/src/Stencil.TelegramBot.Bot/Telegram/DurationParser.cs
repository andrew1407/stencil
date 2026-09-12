namespace Stencil.TelegramBot.Bot.Telegram;

public enum DurationUnit
{
    Day,
    Week,
    Month,
}

// Resolved against a base instant so months are calendar months (not a fixed 30 days).
public sealed record ParsedDuration(DurationUnit Unit, int Count)
{
    public DateTimeOffset From(DateTimeOffset baseTime) => Unit switch
    {
        DurationUnit.Day => baseTime.AddDays(Count),
        DurationUnit.Week => baseTime.AddDays(7L * Count),
        DurationUnit.Month => baseTime.AddMonths(Count),
        _ => baseTime,
    };

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

// A unit word (singular, plural or short form) with an optional count that may lead or trail it:
// "day", "days 3", "1 week", "fortnight", "3d", "1mo"; "never"/"forever" mean "clear the expiry".
public static class DurationParser
{
    // Keeps well clear of DateTimeOffset overflow.
    private const int _maxCount = 1000;

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
    private static bool tryMapUnit(string word, out DurationUnit unit, out int multiplier)
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
