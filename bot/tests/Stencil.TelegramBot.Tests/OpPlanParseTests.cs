using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// §1 extraction and the accept/reject line: fence stripping, first-balanced-object extraction,
/// the chat-only fallback, the unknown-op skip-with-warning, and strict validation of a known op.
/// </summary>
public sealed class OpPlanParseTests
{
    [Fact]
    public void ParsesAWellFormedPlanWithActionsAndVariants()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """
            {"version":1,"reply":"Done!","actions":[{"op":"rotate","dir":"right","times":2}],
             "variants":[{"label":"tinted","actions":[{"op":"filter","mode":"custom","tint":"#ff0000"}]}]}
            """);

        Assert.Null(result.Error);
        Assert.Empty(result.Warnings);
        OpPlan plan = result.Plan!;
        Assert.Equal("Done!", plan.Reply);
        RotateAction rotate = Assert.IsType<RotateAction>(Assert.Single(plan.Actions));
        Assert.Equal("right", rotate.Dir);
        Assert.Equal(2, rotate.Times);
        OpVariant variant = Assert.Single(plan.Variants);
        Assert.Equal("tinted", variant.Label);
        FilterAction filter = Assert.IsType<FilterAction>(Assert.Single(variant.Actions));
        Assert.Equal("custom", filter.Mode);
        Assert.Equal("#ff0000", filter.Tint);
    }

    [Fact]
    public void StripsMarkdownFencesAroundThePlan()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            "```json\n{\"reply\":\"ok\",\"actions\":[{\"op\":\"filter\",\"mode\":\"bw\"}]}\n```");

        Assert.Null(result.Error);
        Assert.Equal("ok", result.Plan!.Reply);
        Assert.IsType<FilterAction>(Assert.Single(result.Plan.Actions));
    }

    [Fact]
    public void TakesTheFirstBalancedObjectOutOfSurroundingProse()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            "Sure — here is the plan:\n{\"reply\":\"cropped\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\"}}]}\nHope that helps!");

        Assert.Null(result.Error);
        Assert.Equal("cropped", result.Plan!.Reply);
        CropAction crop = Assert.IsType<CropAction>(Assert.Single(result.Plan.Actions));
        Assert.Equal("x1=10%", crop.Spec);
    }

    [Fact]
    public void NoJsonObjectAtAllIsAChatOnlyTurnNotAnError()
    {
        OpPlanParseResult result = OpPlanParser.Parse("Just chatting — no edits needed.");

        Assert.Null(result.Error);
        Assert.Empty(result.Warnings);
        Assert.Equal("Just chatting — no edits needed.", result.Plan!.Reply);
        Assert.Empty(result.Plan.Actions);
        Assert.Empty(result.Plan.Variants);
    }

    [Fact]
    public void UnknownOpIsSkippedWithAWarningWhileKnownOpsSurvive()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"zoom","level":2},{"op":"rotate","dir":"left"}]}""");

        Assert.Null(result.Error);
        string warning = Assert.Single(result.Warnings);
        Assert.Contains("zoom", warning);
        RotateAction rotate = Assert.IsType<RotateAction>(Assert.Single(result.Plan!.Actions));
        Assert.Equal(1, rotate.Times); // default when omitted
    }

    [Fact]
    public void CopyOpIsUnknownHereAndSkippedWithAWarningNotAFailedPlan()
    {
        // §10 `copy` targets a clipboard; a chat surface has none, so the bot keeps it an
        // UNKNOWN op — dropped with the §1 warning while the rest of the plan runs.
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"copy"},{"op":"filter","mode":"sepia"}]}""");

        Assert.Null(result.Error);
        string warning = Assert.Single(result.Warnings);
        Assert.Contains("copy", warning);
        FilterAction filter = Assert.IsType<FilterAction>(Assert.Single(result.Plan!.Actions));
        Assert.Equal("sepia", filter.Mode);
    }

    [Fact]
    public void KnownOpWithInvalidParamsFailsTheWholePlan()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"filter","mode":"bw"},{"op":"rotate","dir":"right","times":7}]}""");

        Assert.Null(result.Plan);
        Assert.Contains("rotate", result.Error);
    }

    [Fact]
    public void UnexpectedFieldOnAKnownOpFailsThePlan()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"rotate","dir":"left","angle":45}]}""");

        Assert.Null(result.Plan);
        Assert.Contains("angle", result.Error);
    }

    [Fact]
    public void MissingOrEmptyReplyIsToleratedWithAWarning()
    {
        // §1 reply tolerance: "Done." + a warning, the plan itself survives.
        OpPlanParseResult missing = OpPlanParser.Parse("""{"actions":[{"op":"rotate","dir":"left"}]}""");
        Assert.Null(missing.Error);
        Assert.Equal("Done.", missing.Plan!.Reply);
        Assert.Single(missing.Plan.Actions);
        Assert.Contains(missing.Warnings, w => w.Contains("omitted its reply"));

        // An EMPTY plan says so — a bare "Done." would read as a success that
        // never occurred (contract §1).
        OpPlanParseResult blank = OpPlanParser.Parse("""{"reply":"","actions":[]}""");
        Assert.Null(blank.Error);
        Assert.Contains("empty plan", blank.Plan!.Reply);
        Assert.DoesNotContain(blank.Warnings, w => w.Contains("still ran"));
    }

    [Fact]
    public void VersionOtherThanOneIsAcceptedAndIgnored()
    {
        OpPlanParseResult result = OpPlanParser.Parse("""{"version":7,"reply":"ok","actions":[]}""");
        Assert.Null(result.Error);
        Assert.Equal("ok", result.Plan!.Reply);
    }

    // ── crop ──
}
