using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Application.Llm.Plan;

namespace Stencil.TelegramBot.Tests.Llm.Plan;

/// <summary>§2.1 variants: a misplaced op costs its own variant and nothing more, and a huge invalid value is never echoed back.</summary>
public sealed class OpPlanVariantTests
{
    [Theory]
    [InlineData("""{"op":"undo"}""", "top-level")]
    [InlineData("""{"op":"redo","steps":2}""", "top-level")]
    [InlineData("""{"op":"reset"}""", "top-level")]
    [InlineData("""{"op":"clearChat"}""", "editor-settings")]
    [InlineData("""{"op":"clear"}""", "editor-settings")]
    [InlineData("""{"op":"lineStyle","color":"#00ff00"}""", "editor-settings")]
    [InlineData("""{"op":"openUrl","url":"https://x/a.png"}""", "editor-settings")]
    [InlineData("""{"op":"renameProject","name":"n"}""", "editor-settings")]
    [InlineData("""{"op":"describe","text":"d"}""", "§10")]
    [InlineData("""{"op":"blankColor","color":"#dbeafe"}""", "editor-settings")]
    [InlineData("""{"op":"projectColor","color":""}""", "editor-settings")]
    [InlineData("""{"op":"export","what":"layout"}""", "§10")]
    public void Should_Drop_Profile_Ops_With_A_Warning_When_Inside_Variants_And_Ask_Previews(string action, string marker)
    {
        OpPlanParseResult inVariant = RecordedPlans.Parse(
            $$"""{"reply":"x","variants":[{"label":"v","actions":[{{action}}]}]}""");
        Assert.Null(inVariant.Error);
        Assert.Empty(inVariant.Plan!.Variants);
        Assert.Contains(marker, Assert.Single(inVariant.Warnings));

        OpPlanParseResult inPreview = RecordedPlans.Parse(
            $$$"""
            {"reply":"x","ask":{"question":"Which?","options":[
              {"label":"A","actions":[{{{action}}}]},{"label":"B"}]}}
            """);
        Assert.Null(inPreview.Error);
        Assert.Equal(2, inPreview.Plan!.Ask!.Options.Count);
        Assert.Contains(marker, Assert.Single(inPreview.Warnings));
    }

    // §1: losing a whole turn's work to one misplaced op taught the user nothing — the
    // top-level actions and the well-formed variants still run.
    [Fact]
    public void Should_Drop_One_Misplaced_Variant_While_The_Actions_And_Good_Variants_Survive()
    {
        OpPlanParseResult result = RecordedPlans.Parse(
            """
            {"reply":"three takes","actions":[{"op":"filter","mode":"bw"}],"variants":[
              {"label":"sepia","actions":[{"op":"filter","mode":"sepia"}]},
              {"label":"wiped","actions":[{"op":"clear"}]},
              {"label":"turned","actions":[{"op":"rotate","dir":"left"}]}]}
            """);

        Assert.Null(result.Error);
        Assert.IsType<FilterAction>(Assert.Single(result.Plan!.Actions));
        Assert.Equal(["sepia", "turned"], result.Plan.Variants.Select(v => v.Label));
        string warning = Assert.Single(result.Warnings);
        Assert.Contains("variant 2", warning);   // named by position AND label
        Assert.Contains("wiped", warning);
        Assert.Contains("\"clear\"", warning);
    }

    [Fact]
    public void Should_Stay_A_Valid_Plan_When_The_Only_Variant_Is_Misplaced()
    {
        OpPlanParseResult result = RecordedPlans.Parse(
            """{"reply":"here you go","variants":[{"label":"saved","actions":[{"op":"save"}]}]}""");

        Assert.Null(result.Error);
        Assert.Equal("here you go", result.Plan!.Reply);
        Assert.Empty(result.Plan.Actions);
        Assert.Empty(result.Plan.Variants);
        Assert.Contains("saved", Assert.Single(result.Warnings));
    }

    [Fact]
    public void Should_Still_Note_An_Unknown_Op_Met_Before_A_Variant_Is_Dropped()
    {
        // Core's §1 walk notes each unknown op as it meets it, before a misplaced one costs the variant.
        OpPlanParseResult result = RecordedPlans.Parse(
            """{"reply":"x","variants":[{"label":"v","actions":[{"op":"sharpen"},{"op":"undo"}]}]}""");

        Assert.Null(result.Error);
        Assert.Empty(result.Plan!.Variants);
        Assert.Equal(2, result.Warnings.Count);
        Assert.Contains("sharpen", result.Warnings[0]);
        Assert.Contains("top-level", result.Warnings[1]);
    }

    [Fact]
    public void Should_Fail_The_Whole_Plan_For_A_Known_Op_With_Bad_Params_Inside_A_Variant()
    {
        // The leniency is only for MISPLACED ops — every other strictness is unchanged.
        OpPlanParseResult result = RecordedPlans.Parse(
            """{"reply":"x","variants":[{"label":"v","actions":[{"op":"rotate","dir":"up"}]}]}""");

        Assert.Null(result.Plan);
        Assert.Contains("rotate", result.Error);
    }

    [Fact]
    public void Should_Tolerate_Undeclared_Keys_In_A_Variant_While_Its_Label_Stays_A_String()
    {
        // The envelope's variant objects carry allowUnknown (only ops are strict about fields).
        OpPlanParseResult ok = RecordedPlans.Parse(
            """{"reply":"x","variants":[{"label":"v","actions":[],"note":"x"}]}""");
        Assert.Null(ok.Error);
        Assert.Equal("v", Assert.Single(ok.Plan!.Variants).Label);
        Assert.NotNull(RecordedPlans.Parse("""{"reply":"x","variants":[{"label":7,"actions":[]}]}""").Error);
    }

    [Fact]
    public void Should_Never_Echo_Huge_Invalid_Values_Into_The_Error_Message()
    {
        string token = new('x', 500);
        OpPlanParseResult result = RecordedPlans.Parse(
            $$$"""{"reply":"ok","actions":[{"op":"crop","spec":{"x1":"{{{token}}}"}}]}""");
        Assert.Null(result.Plan);
        Assert.DoesNotContain(token, result.Error);
        Assert.True(result.Error!.Length < 150); // the 500-char token names its grammar, not its value
    }
}