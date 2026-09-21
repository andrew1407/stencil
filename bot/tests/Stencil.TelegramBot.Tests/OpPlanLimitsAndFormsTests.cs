using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Application.Llm.Plan;

namespace Stencil.TelegramBot.Tests;

/// <summary>The shared envelope limits (16 actions / 8 variants) and the exactly-one-form rules for <c>page</c>, <c>blank</c> and <c>formula</c>.</summary>
public sealed class OpPlanLimitsAndFormsTests
{
    [Fact]
    public void Should_Fail_The_Plan_For_More_Than_16_Actions()
    {
        string actions = string.Join(',', Enumerable.Repeat("""{"op":"rotate","dir":"left"}""", 17));
        OpPlanParseResult result = OpPlanParser.Parse($$"""{"reply":"ok","actions":[{{actions}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("16", result.Error);
    }

    [Fact]
    public void Should_Fail_The_Plan_For_More_Than_8_Variants()
    {
        string variants = string.Join(',', Enumerable.Repeat("""{"label":"v","actions":[]}""", 9));
        OpPlanParseResult result = OpPlanParser.Parse($$"""{"reply":"ok","variants":[{{variants}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("8", result.Error);
    }

    [Fact]
    public void Should_Fail_The_Plan_For_More_Than_16_Actions_Inside_A_Variant()
    {
        string actions = string.Join(',', Enumerable.Repeat("""{"op":"rotate","dir":"left"}""", 17));
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"ok","variants":[{"label":"v","actions":[{{actions}}]}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("16", result.Error);
    }

    // ── §2.1 multi-image ops: `image` switches to a turn attachment, `save` persists ──

    [Fact]
    public void Should_Take_A_Format_Or_Custom_Cm_Dims_But_Never_Both_For_Page()
    {
        OpPlanParseResult custom = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"page","width":20,"height":30}]}""");
        Assert.Null(custom.Error);
        PageAction page = Assert.IsType<PageAction>(Assert.Single(custom.Plan!.Actions));
        Assert.Null(page.Format);
        Assert.Equal(20, page.WidthCm);
        Assert.Equal(30, page.HeightCm);

        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"x","actions":[{"op":"page","format":"a4","width":20,"height":30}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"x","actions":[{"op":"page","width":20}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"x","actions":[{"op":"page","width":0.05,"height":30}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"x","actions":[{"op":"page","width":20,"height":501}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"page"}]}""").Error);
    }

    [Fact]
    public void Should_Take_Optional_Cm_Dims_Both_Or_Neither_For_Blank()
    {
        OpPlanParseResult ok = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"blank","color":"#ffffff","width":10,"height":15}]}""");
        Assert.Null(ok.Error);
        BlankAction blank = Assert.IsType<BlankAction>(Assert.Single(ok.Plan!.Actions));
        Assert.Equal(10, blank.WidthCm);
        Assert.Equal(15, blank.HeightCm);

        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"x","actions":[{"op":"blank","color":"#ffffff","width":10}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"x","actions":[{"op":"blank","color":"#ffffff","width":10,"height":501}]}""").Error);
    }

    [Fact]
    public void Should_Let_Formula_Enabled_Ride_Alone_And_Allow_An_Empty_Expr()
    {
        OpPlanParseResult off = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"formula","enabled":false}]}""");
        Assert.Null(off.Error);
        FormulaAction formula = Assert.IsType<FormulaAction>(Assert.Single(off.Plan!.Actions));
        Assert.False(formula.Enabled);
        Assert.Null(formula.Axis);

        // An empty expr clears that axis (contract §2).
        OpPlanParseResult clear = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"formula","axis":"x","expr":""}]}""");
        Assert.Null(clear.Error);
        Assert.Equal("", Assert.IsType<FormulaAction>(clear.Plan!.Actions[0]).Expr);

        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"x","actions":[{"op":"formula","enabled":false,"axis":"x","expr":"x"}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"x","actions":[{"op":"formula","enabled":"off"}]}""").Error);
    }

    // ── the widened variant / ask-preview bans (§1: dropped, never fatal) ──
}
