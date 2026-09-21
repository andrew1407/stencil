using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Bot.Telegram.Commands;

namespace Stencil.TelegramBot.Tests;

/// <summary>The pure <c>/draw</c> point parsing: pixel and percentage coordinates, rectangle expansion, and rejection of malformed tokens.</summary>
public sealed class DrawArgumentsTests
{
    [Fact]
    public void Should_Parse_Pixel_Points()
    {
        bool ok = DrawArguments.TryParsePoints(["10,20", "30,40"], 100, 100, out var points, out var error);
        Assert.True(ok);
        Assert.Null(error);
        Assert.Equal(new LayoutPoint(10, 20), points[0]);
        Assert.Equal(new LayoutPoint(30, 40), points[1]);
    }

    [Fact]
    public void Should_Resolve_Percentages_Against_Dimensions()
    {
        bool ok = DrawArguments.TryParsePoints(["50%,25%"], 200, 400, out var points, out _);
        Assert.True(ok);
        Assert.Equal(new LayoutPoint(100, 100), points[0]);
    }

    [Theory]
    [InlineData("10")]      // missing the y component
    [InlineData("a,b")]     // non-numeric
    [InlineData("10,")]     // empty component
    public void Should_Reject_Malformed_Tokens(string token)
    {
        bool ok = DrawArguments.TryParsePoints([token], 100, 100, out _, out var error);
        Assert.False(ok);
        Assert.NotNull(error);
    }

    [Fact]
    public void Should_Fail_For_An_Empty_Token_List()
    {
        bool ok = DrawArguments.TryParsePoints([], 100, 100, out _, out var error);
        Assert.False(ok);
        Assert.NotNull(error);
    }

    [Fact]
    public void Should_Expand_Two_Rectangle_Corners_To_Four()
    {
        var corners = DrawArguments.Rectangle(new LayoutPoint(0, 0), new LayoutPoint(10, 20));
        Assert.Equal(4, corners.Count);
        Assert.Equal(new LayoutPoint(0, 0), corners[0]);
        Assert.Equal(new LayoutPoint(10, 0), corners[1]);
        Assert.Equal(new LayoutPoint(10, 20), corners[2]);
        Assert.Equal(new LayoutPoint(0, 20), corners[3]);
    }
}
