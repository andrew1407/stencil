using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>§2.1 <c>image</c> and <c>save</c>: the attachment index, the bounded name, and the §10 path that is trimmed, bounded and never a URL.</summary>
public sealed class OpPlanSaveTests
{
    [Fact]
    public void Should_Parse_Image_And_Save_With_Their_Optional_Name()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """
            {"reply":"two","actions":[
              {"op":"image","index":2},{"op":"save","name":"portrait 1"},{"op":"save"}]}
            """);

        Assert.Null(result.Error);
        Assert.Equal(2, Assert.IsType<ImageAction>(result.Plan!.Actions[0]).Index);
        Assert.Equal("portrait 1", Assert.IsType<SaveAction>(result.Plan.Actions[1]).Name);
        Assert.Null(Assert.IsType<SaveAction>(result.Plan.Actions[2]).Name);
    }

    [Theory]
    [InlineData("""{"op":"image","index":0}""")]
    [InlineData("""{"op":"image","index":-1}""")]
    [InlineData("""{"op":"image","index":1.5}""")]
    [InlineData("""{"op":"image","index":"1"}""")]
    [InlineData("""{"op":"image"}""")]
    [InlineData("""{"op":"image","index":1,"name":"x"}""")]
    public void Should_Require_A_Whole_Attachment_Number_For_Image_Index(string action)
    {
        OpPlanParseResult result = OpPlanParser.Parse($$"""{"reply":"x","actions":[{{action}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("image", result.Error);
    }

    [Fact]
    public void Should_Bound_The_Save_Name_To_120_Characters()
    {
        string name = new('x', 121);
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"x","actions":[{"op":"save","name":"{{name}}"}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("120", result.Error);
    }

    [Fact]
    public void Should_Trim_The_Save_Path_And_Treat_Empty_After_Trim_As_Absent()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"x","actions":[{"op":"save","name":"p","path":"  ~/Downloads  "},{"op":"save","path":"   "}]}""");
        Assert.Null(result.Error);
        Assert.Equal("~/Downloads", Assert.IsType<SaveAction>(result.Plan!.Actions[0]).Path);
        Assert.Null(Assert.IsType<SaveAction>(result.Plan.Actions[1]).Path);
    }

    [Fact]
    public void Should_Bound_The_Save_Path_To_1024_Characters()
    {
        string path = new('p', 1025);
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"x","actions":[{"op":"save","path":"{{path}}"}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("1024", result.Error);
    }

    [Theory]
    [InlineData("""{"op":"save","path":"https://x.example/out.png"}""")]
    [InlineData("""{"op":"save","path":"file:///tmp/p.stencil"}""")]
    [InlineData("""{"op":"save","path":7}""")]
    public void Should_Require_A_Local_Path_String_For_Save_Path(string action)
    {
        OpPlanParseResult result = OpPlanParser.Parse($$"""{"reply":"x","actions":[{{action}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("path", result.Error);
    }

    [Theory]
    [InlineData("""{"op":"image","index":1}""")]
    [InlineData("""{"op":"save"}""")]
    public void Should_Cost_Only_That_Variant_When_Image_And_Save_Sit_Inside_A_Variant_Or_Preview(string action)
    {
        OpPlanParseResult inVariant = OpPlanParser.Parse(
            $$"""{"reply":"x","variants":[{"label":"v","actions":[{{action}}]}]}""");
        Assert.Null(inVariant.Error);
        Assert.Empty(inVariant.Plan!.Variants);
        Assert.Contains("top-level", Assert.Single(inVariant.Warnings));

        OpPlanParseResult inPreview = OpPlanParser.Parse(
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
