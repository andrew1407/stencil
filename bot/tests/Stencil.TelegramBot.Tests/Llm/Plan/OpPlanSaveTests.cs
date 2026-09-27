using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Application.Llm.Plan;

namespace Stencil.TelegramBot.Tests.Llm.Plan;

/// <summary>§2.1 <c>image</c> and <c>save</c>: the attachment index, the bounded name, and the §10 path that is trimmed, bounded and never a URL.</summary>
public sealed class OpPlanSaveTests
{
    [Fact]
    public void Should_Parse_Image_And_Save_With_Their_Optional_Name()
    {
        OpPlanParseResult result = RecordedPlans.Parse(
            """
            {"reply":"two","actions":[
              {"op":"image","index":2},{"op":"save","name":"portrait 1"},{"op":"save"}]}
            """);

        Assert.Null(result.Error);
        Assert.Equal(2, Assert.IsType<ImageAction>(result.Plan!.Actions[0]).Index);
        Assert.Equal("portrait 1", Assert.IsType<SaveAction>(result.Plan.Actions[1]).Name);
        Assert.Null(Assert.IsType<SaveAction>(result.Plan.Actions[2]).Name);
    }

    [Fact]
    public void Should_Trim_The_Save_Path_And_Treat_Empty_After_Trim_As_Absent()
    {
        OpPlanParseResult result = RecordedPlans.Parse(
            """{"reply":"x","actions":[{"op":"save","name":"p","path":"  ~/Downloads  "},{"op":"save","path":"   "}]}""");
        Assert.Null(result.Error);
        Assert.Equal("~/Downloads", Assert.IsType<SaveAction>(result.Plan!.Actions[0]).Path);
        Assert.Null(Assert.IsType<SaveAction>(result.Plan.Actions[1]).Path);
    }

    [Theory]
    [InlineData("""{"op":"image","index":1}""")]
    [InlineData("""{"op":"save"}""")]
    public void Should_Cost_Only_That_Variant_When_Image_And_Save_Sit_Inside_A_Variant_Or_Preview(string action)
    {
        OpPlanParseResult inVariant = RecordedPlans.Parse(
            $$"""{"reply":"x","variants":[{"label":"v","actions":[{{action}}]}]}""");
        Assert.Null(inVariant.Error);
        Assert.Empty(inVariant.Plan!.Variants);
        Assert.Contains("top-level", Assert.Single(inVariant.Warnings));

        OpPlanParseResult inPreview = RecordedPlans.Parse(
            $$$"""
            {"reply":"x","ask":{"question":"Which?","options":[
              {"label":"A","actions":[{{{action}}}]},{"label":"B"}]}}
            """);
        Assert.Null(inPreview.Error);
        Assert.Equal(["A", "B"], inPreview.Plan!.Ask!.Options.Select(o => o.Label));
        Assert.Contains("top-level", Assert.Single(inPreview.Warnings));
    }

    // ── §10 connection ops (carried into the bot profile) ──
}
