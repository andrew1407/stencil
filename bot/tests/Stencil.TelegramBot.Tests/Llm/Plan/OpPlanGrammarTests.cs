using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Application.Llm.Plan;

namespace Stencil.TelegramBot.Tests.Llm.Plan;

/// <summary>The per-op value grammars of §2: filter tints, layout lines and their defaults, formula charsets, ISO page names, blank colours and the frame index/indices pair.</summary>
public sealed class OpPlanGrammarTests
{
    [Fact]
    public void Should_Require_A_Tint_For_Filter_Custom_And_Forbid_It_For_Others()
    {
        Assert.NotNull(RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"filter","mode":"custom"}]}""").Error);
        Assert.NotNull(RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"filter","mode":"custom","tint":"red"}]}""").Error);
        Assert.NotNull(RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"filter","mode":"bw","tint":"#ff0000"}]}""").Error);

        OpPlanParseResult ok = RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"filter","mode":"custom","tint":"#00Ff00"}]}""");
        Assert.Null(ok.Error);
        Assert.Equal("#00Ff00", Assert.IsType<FilterAction>(ok.Plan!.Actions[0]).Tint);
    }

    // ── layout ──

    [Fact]
    public void Should_Give_Layout_Lines_The_Shared_Defaults_For_Omitted_Fields()
    {
        OpPlanParseResult result = RecordedPlans.Parse(
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

    // ── formula ──

    // ── page / blank ──

    [Fact]
    public void Should_Take_Hex_Or_Css_Name_Colours_And_An_Optional_Format_For_Blank()
    {
        OpPlanParseResult hex = RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"blank","color":"#ffffff","format":"a4"}]}""");
        Assert.Null(hex.Error);
        BlankAction blank = Assert.IsType<BlankAction>(hex.Plan!.Actions[0]);
        Assert.Equal("a4", blank.Format);

        Assert.Null(RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"blank","color":"cornflowerblue"}]}""").Error);
        Assert.NotNull(RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"blank","color":"#ffff"}]}""").Error);
        Assert.NotNull(RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"blank"}]}""").Error);
    }

    // ── frame ──

    [Fact]
    public void Should_Take_Exactly_One_Of_Index_Or_Indices_For_Frame()
    {
        OpPlanParseResult single = RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"frame","index":3}]}""");
        Assert.Null(single.Error);
        Assert.Equal([3], Assert.IsType<FrameAction>(single.Plan!.Actions[0]).Indices);

        OpPlanParseResult multi = RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"frame","indices":[0,30,60]}]}""");
        Assert.Null(multi.Error);
        Assert.Equal([0, 30, 60], Assert.IsType<FrameAction>(multi.Plan!.Actions[0]).Indices);

        Assert.NotNull(RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"frame","index":1,"indices":[2]}]}""").Error);
        Assert.NotNull(RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"frame"}]}""").Error);
        Assert.NotNull(RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"frame","index":-1}]}""").Error);
    }

    // ── limits ──
}
