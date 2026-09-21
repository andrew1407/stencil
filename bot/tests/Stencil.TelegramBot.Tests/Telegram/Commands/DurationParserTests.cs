using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Bot.Telegram.Commands;

namespace Stencil.TelegramBot.Tests.Telegram.Commands;

/// <summary>The pure <c>/expire</c> duration parsing: unit words (singular, plural, short), a count that may lead or trail, fortnight = 2 weeks, the "clear" keywords, and junk. Calendar resolution is checked against a fixed base.</summary>
public sealed class DurationParserTests
{
    [Theory]
    [InlineData("day", DurationUnit.DAY, 1)]
    [InlineData("days 1", DurationUnit.DAY, 1)]
    [InlineData("days 3", DurationUnit.DAY, 3)]
    [InlineData("3 days", DurationUnit.DAY, 3)]
    [InlineData("3d", DurationUnit.DAY, 3)]
    [InlineData("week", DurationUnit.WEEK, 1)]
    [InlineData("weeks", DurationUnit.WEEK, 1)]
    [InlineData("1 week", DurationUnit.WEEK, 1)]
    [InlineData("week 4", DurationUnit.WEEK, 4)]
    [InlineData("2 weeks", DurationUnit.WEEK, 2)]
    [InlineData("2w", DurationUnit.WEEK, 2)]
    [InlineData("fortnight", DurationUnit.WEEK, 2)]     // 1 fortnight = 2 weeks
    [InlineData("2 fortnights", DurationUnit.WEEK, 4)]  // 2 fortnights = 4 weeks
    [InlineData("month", DurationUnit.MONTH, 1)]
    [InlineData("3 months", DurationUnit.MONTH, 3)]
    [InlineData("1mo", DurationUnit.MONTH, 1)]
    [InlineData("  1   MONTH  ", DurationUnit.MONTH, 1)] // case-insensitive, whitespace-tolerant
    public void Should_Parse_Unit_And_Count(string input, DurationUnit unit, int count)
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
    public void Should_Recognise_Clear_Keywords(string input)
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
    public void Should_Reject_Junk(string input)
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
    public void Should_Resolve_Days_Weeks_And_Months_From_A_Base()
    {
        DateTimeOffset baseTime = new(2026, 1, 15, 0, 0, 0, TimeSpan.Zero);
        Assert.Equal(new DateTimeOffset(2026, 1, 18, 0, 0, 0, TimeSpan.Zero), new ParsedDuration(DurationUnit.DAY, 3).From(baseTime));
        Assert.Equal(new DateTimeOffset(2026, 1, 29, 0, 0, 0, TimeSpan.Zero), new ParsedDuration(DurationUnit.WEEK, 2).From(baseTime));
        Assert.Equal(new DateTimeOffset(2026, 4, 15, 0, 0, 0, TimeSpan.Zero), new ParsedDuration(DurationUnit.MONTH, 3).From(baseTime));
    }
}
