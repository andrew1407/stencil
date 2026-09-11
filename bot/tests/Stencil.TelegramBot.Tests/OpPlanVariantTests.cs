using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// §2.1 variants: a misplaced op costs its own variant and nothing more, a dropped variant takes
/// its warnings with it, and a huge invalid value is never echoed back.
/// </summary>
public sealed class OpPlanVariantTests
{
    [Theory]
    [InlineData("""{"op":"undo"}""", "top-level")]
    [InlineData("""{"op":"redo","steps":2}""", "top-level")]
    [InlineData("""{"op":"reset"}""", "top-level")]
    [InlineData("""{"op":"clearChat"}""", "top-level")]
    [InlineData("""{"op":"clear"}""", "§10")]
    [InlineData("""{"op":"lineStyle","color":"#00ff00"}""", "§10")]
    [InlineData("""{"op":"openUrl","url":"https://x/a.png"}""", "§10")]
    [InlineData("""{"op":"renameProject","name":"n"}""", "§10")]
    [InlineData("""{"op":"describe","text":"d"}""", "§10")]
    [InlineData("""{"op":"blankColor","color":"#dbeafe"}""", "§10")]
    [InlineData("""{"op":"projectColor","color":""}""", "§10")]
    [InlineData("""{"op":"export","what":"layout"}""", "§10")]
    public void ProfileOpsInsideVariantsAndAskPreviewsAreDroppedWithAWarning(string action, string marker)
    {
        OpPlanParseResult inVariant = OpPlanParser.Parse(
            $$"""{"reply":"x","variants":[{"label":"v","actions":[{{action}}]}]}""");
        Assert.Null(inVariant.Error);
        Assert.Empty(inVariant.Plan!.Variants);
        Assert.Contains(marker, Assert.Single(inVariant.Warnings));

        OpPlanParseResult inPreview = OpPlanParser.Parse(
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
    public void OneMisplacedVariantIsDroppedWhileTheActionsAndGoodVariantsSurvive()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
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
    public void APlanWhoseONLYVariantIsMisplacedStaysAValidPlan()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"here you go","variants":[{"label":"saved","actions":[{"op":"save"}]}]}""");

        Assert.Null(result.Error);
        Assert.Equal("here you go", result.Plan!.Reply);
        Assert.Empty(result.Plan.Actions);
        Assert.Empty(result.Plan.Variants);
        Assert.Contains("saved", Assert.Single(result.Warnings));
    }

    [Fact]
    public void ADroppedVariantTakesItsOwnWarningsWithIt()
    {
        // Nothing of that variant runs, so its unknown-op note has nothing left to explain.
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"x","variants":[{"label":"v","actions":[{"op":"sharpen"},{"op":"undo"}]}]}""");

        Assert.Null(result.Error);
        Assert.DoesNotContain(result.Warnings, w => w.Contains("sharpen"));
        Assert.Contains("top-level", Assert.Single(result.Warnings));
    }

    [Fact]
    public void AKnownOpWithBadParamsInsideAVariantStillFailsTheWholePlan()
    {
        // The leniency is only for MISPLACED ops — every other strictness is unchanged.
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"x","variants":[{"label":"v","actions":[{"op":"rotate","dir":"up"}]}]}""");

        Assert.Null(result.Plan);
        Assert.Contains("rotate", result.Error);
    }

    [Fact]
    public void AVariantToleratesUndeclaredKeysWhileItsLabelStaysAString()
    {
        // The envelope's variant objects carry allowUnknown (only ops are strict about fields).
        OpPlanParseResult ok = OpPlanParser.Parse(
            """{"reply":"x","variants":[{"label":"v","actions":[],"note":"x"}]}""");
        Assert.Null(ok.Error);
        Assert.Equal("v", Assert.Single(ok.Plan!.Variants).Label);
        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","variants":[{"label":7,"actions":[]}]}""").Error);
    }

    [Fact]
    public void HugeInvalidValuesAreNeverEchoedIntoTheErrorMessage()
    {
        string token = new('x', 500);
        OpPlanParseResult result = OpPlanParser.Parse(
            $$$"""{"reply":"ok","actions":[{"op":"crop","spec":{"x1":"{{{token}}}"}}]}""");
        Assert.Null(result.Plan);
        Assert.DoesNotContain(token, result.Error);
        Assert.True(result.Error!.Length < 150); // the 500-char token names its grammar, not its value
    }
}