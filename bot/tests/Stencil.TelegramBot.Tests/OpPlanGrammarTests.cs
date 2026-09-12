using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The per-op value grammars of §2: filter tints, layout lines and their defaults, formula
/// charsets, ISO page names, blank colours and the frame index/indices pair.
/// </summary>
public sealed class OpPlanGrammarTests
{
    [Fact]
    public void Should_Require_A_Tint_For_Filter_Custom_And_Forbid_It_For_Others()
    {
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"filter","mode":"custom"}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"filter","mode":"custom","tint":"red"}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"filter","mode":"bw","tint":"#ff0000"}]}""").Error);

        OpPlanParseResult ok = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"filter","mode":"custom","tint":"#00Ff00"}]}""");
        Assert.Null(ok.Error);
        Assert.Equal("#00Ff00", Assert.IsType<FilterAction>(ok.Plan!.Actions[0]).Tint);
    }

    // ── layout ──

    [Fact]
    public void Should_Give_Layout_Lines_The_Shared_Defaults_For_Omitted_Fields()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"layout","lines":[{"points":[{"x":1,"y":2},{"x":3,"y":4}]}]}]}""");

        Assert.Null(result.Error);
        LayoutLine line = Assert.Single(Assert.IsType<LayoutAction>(result.Plan!.Actions[0]).Lines);
        Assert.Equal(LayoutLine.DEFAULT_COLOR, line.Color);
        Assert.Equal(LayoutLine.DEFAULT_THICKNESS, line.Thickness);
        Assert.Equal(LayoutLine.DEFAULT_STYLE, line.Style);
        Assert.Equal(LayoutLine.DEFAULT_FILL_COLOR, line.FillColor);
        Assert.Equal(2, line.Points.Count);
        Assert.Equal(3, line.Points[1].X);
    }

    [Fact]
    public void Should_Reject_Bad_Lines_For_Layout()
    {
        // Unknown line field.
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"layout","lines":[{"points":[{"x":1,"y":2}],"z":1}]}]}""").Error);
        // Bad style.
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"layout","lines":[{"points":[{"x":1,"y":2}],"style":"wavy"}]}]}""").Error);
        // Point missing y.
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"layout","lines":[{"points":[{"x":1}]}]}]}""").Error);
    }

    [Fact]
    public void Should_Reject_More_Than_200_Lines_For_Layout()
    {
        string lines = string.Join(',', Enumerable.Repeat("""{"points":[{"x":0,"y":0}]}""", 201));
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"ok","actions":[{"op":"layout","lines":[{{lines}}]}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("200", result.Error);
    }

    // ── formula ──

    [Fact]
    public void Should_Enforce_Charset_And_The_Axis_Variable_For_Formula()
    {
        Assert.Null(OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"formula","axis":"x","expr":"x*2 + (10 / 4) ** 2"}]}""").Error);
        // The other axis letter is invalid…
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"formula","axis":"x","expr":"y*2"}]}""").Error);
        // …and so is any foreign character.
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"formula","axis":"y","expr":"y%2"}]}""").Error);
    }

    [Fact]
    public void Should_Fail_The_Plan_For_A_Formula_Longer_Than_5000_Chars()
    {
        string expr = new('1', 5001);
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"ok","actions":[{"op":"formula","axis":"x","expr":"{{expr}}"}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("5000", result.Error);
    }

    // ── page / blank ──

    [Theory]
    [InlineData("a4", true)]
    [InlineData("c10", true)]
    [InlineData("b0", true)]
    [InlineData("a11", false)]
    [InlineData("A4", false)]  // lowercase only
    [InlineData("d4", false)]
    public void Should_Validate_The_Iso_Format_Name_For_Page(string format, bool ok)
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"ok","actions":[{"op":"page","format":"{{format}}"}]}""");
        Assert.Equal(ok, result.Error is null);
    }

    [Fact]
    public void Should_Take_Hex_Or_Css_Name_Colours_And_An_Optional_Format_For_Blank()
    {
        OpPlanParseResult hex = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"blank","color":"#ffffff","format":"a4"}]}""");
        Assert.Null(hex.Error);
        BlankAction blank = Assert.IsType<BlankAction>(hex.Plan!.Actions[0]);
        Assert.Equal("a4", blank.Format);

        Assert.Null(OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"blank","color":"cornflowerblue"}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"blank","color":"#ffff"}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"blank"}]}""").Error);
    }

    // ── frame ──

    [Fact]
    public void Should_Take_Exactly_One_Of_Index_Or_Indices_For_Frame()
    {
        OpPlanParseResult single = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"frame","index":3}]}""");
        Assert.Null(single.Error);
        Assert.Equal([3], Assert.IsType<FrameAction>(single.Plan!.Actions[0]).Indices);

        OpPlanParseResult multi = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"frame","indices":[0,30,60]}]}""");
        Assert.Null(multi.Error);
        Assert.Equal([0, 30, 60], Assert.IsType<FrameAction>(multi.Plan!.Actions[0]).Indices);

        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"frame","index":1,"indices":[2]}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"frame"}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"frame","index":-1}]}""").Error);
    }

    [Fact]
    public void Should_Reject_More_Than_32_Indices_For_Frame()
    {
        string indices = string.Join(',', Enumerable.Range(0, 33));
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"ok","actions":[{"op":"frame","indices":[{{indices}}]}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("32", result.Error);
    }

    // ── limits ──
}
