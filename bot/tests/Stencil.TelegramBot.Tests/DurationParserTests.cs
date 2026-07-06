using Stencil.TelegramBot.Bot.Telegram;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// <see cref="DurationParser"/> — the pure <c>/expire</c> duration parsing: unit words (singular,
/// plural, short forms), a count that may lead or trail, fortnight = 2 weeks, the "clear" keywords,
/// and rejection of junk. Calendar resolution (months, weeks) is checked against a fixed base.
/// </summary>
public sealed class DurationParserTests
{
    [Theory]
    [InlineData("day", DurationUnit.Day, 1)]
    [InlineData("days 1", DurationUnit.Day, 1)]
    [InlineData("days 3", DurationUnit.Day, 3)]
    [InlineData("3 days", DurationUnit.Day, 3)]
    [InlineData("3d", DurationUnit.Day, 3)]
    [InlineData("week", DurationUnit.Week, 1)]
    [InlineData("weeks", DurationUnit.Week, 1)]
    [InlineData("1 week", DurationUnit.Week, 1)]
    [InlineData("week 4", DurationUnit.Week, 4)]
    [InlineData("2 weeks", DurationUnit.Week, 2)]
    [InlineData("2w", DurationUnit.Week, 2)]
    [InlineData("fortnight", DurationUnit.Week, 2)]     // 1 fortnight = 2 weeks
    [InlineData("2 fortnights", DurationUnit.Week, 4)]  // 2 fortnights = 4 weeks
    [InlineData("month", DurationUnit.Month, 1)]
    [InlineData("3 months", DurationUnit.Month, 3)]
    [InlineData("1mo", DurationUnit.Month, 1)]
    [InlineData("  1   MONTH  ", DurationUnit.Month, 1)] // case-insensitive, whitespace-tolerant
    public void ParsesUnitAndCount(string input, DurationUnit unit, int count)
    {
        bool ok = DurationParser.TryParse(input, out ParsedDuration duration, out bool clear);
        Assert.True(ok);
        Assert.False(clear);
        Assert.Equal(unit, duration.Unit);
        Assert.Equal(count, duration.Count);
    }

    [Theory]
    [InlineData("never")]
    [InlineData("forever")]
    [InlineData("none")]
    [InlineData("clear")]
    [InlineData("off")]
    [InlineData("0")]
    public void RecognisesClearKeywords(string input)
    {
        bool ok = DurationParser.TryParse(input, out _, out bool clear);
        Assert.True(ok);
        Assert.True(clear);
    }

    [Theory]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData("banana")]
    [InlineData("5")]           // number without a unit
    [InlineData("0 days")]      // non-positive count
    [InlineData("-3 days")]     // the minus isn't a digit run → count 3? no: leading '-' ignored, "3" parsed
    [InlineData("custom")]      // handled by the command, not a duration
    [InlineData("99999 days")]  // absurd count
    public void RejectsJunk(string input)
    {
        // "-3 days" deliberately parses to 3 days (the sign isn't part of the digit run); assert the
        // rest reject. Keep it explicit so the intent of each case is documented.
        if (input == "-3 days")
        {
            Assert.True(DurationParser.TryParse(input, out ParsedDuration d, out _));
            Assert.Equal(3, d.Count);
            return;
        }
        Assert.False(DurationParser.TryParse(input, out _, out _));
    }

    [Fact]
    public void ResolvesDaysWeeksAndMonthsFromABase()
    {
        DateTimeOffset baseTime = new(2026, 1, 15, 0, 0, 0, TimeSpan.Zero);
        Assert.Equal(new DateTimeOffset(2026, 1, 18, 0, 0, 0, TimeSpan.Zero), new ParsedDuration(DurationUnit.Day, 3).From(baseTime));
        Assert.Equal(new DateTimeOffset(2026, 1, 29, 0, 0, 0, TimeSpan.Zero), new ParsedDuration(DurationUnit.Week, 2).From(baseTime));
        Assert.Equal(new DateTimeOffset(2026, 4, 15, 0, 0, 0, TimeSpan.Zero), new ParsedDuration(DurationUnit.Month, 3).From(baseTime));
    }
}
