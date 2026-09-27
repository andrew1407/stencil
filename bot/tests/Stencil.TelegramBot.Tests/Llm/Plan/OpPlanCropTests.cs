using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Application.Llm.Plan;

namespace Stencil.TelegramBot.Tests.Llm.Plan;

/// <summary>§3.2 <c>crop</c>: the spec grammar, the aspect key inside and beside the spec, and the fold that tolerates the duplicate but refuses a conflict.</summary>
public sealed class OpPlanCropTests
{
    [Theory]
    [InlineData("""{"x1":"10%","x2":"-10%","y1":"0","y2":"4cm"}""", "x1=10% x2=-10% y1=0 y2=4cm")]
    [InlineData("""{"y2":"12.5px"}""", "y2=12.5px")]
    [InlineData("""{"x1":"3in"}""", "x1=3in")]
    public void Should_Join_Validated_Tokens_With_Spaces_For_Crop(string spec, string expected)
    {
        OpPlanParseResult result = RecordedPlans.Parse(
            $$"""{"reply":"ok","actions":[{"op":"crop","spec":{{spec}}}]}""");

        Assert.Null(result.Error);
        Assert.Equal(expected, Assert.IsType<CropAction>(result.Plan!.Actions[0]).Spec);
    }

    [Fact]
    public void Should_Accept_An_Aspect_Key_And_Keep_Its_Value_For_Crop()
    {
        OpPlanParseResult result = RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"crop","spec":{"x1":"10%","aspect":"4:3"}}]}""");

        Assert.Null(result.Error);
        Assert.Equal("x1=10% aspect=4:3", Assert.IsType<CropAction>(result.Plan!.Actions[0]).Spec);
    }

    [Fact]
    public void Should_Accept_An_Aspect_Only_Spec_For_Crop()
    {
        OpPlanParseResult result = RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"crop","spec":{"aspect":"16:9"}}]}""");

        Assert.Null(result.Error);
        Assert.Equal("aspect=16:9", Assert.IsType<CropAction>(result.Plan!.Actions[0]).Spec);
    }

    [Fact]
    public void Should_Fold_In_A_Crop_Aspect_Beside_The_Spec()
    {
        // §1 tolerance: models sometimes emit "aspect" beside "spec" — same validation, folded.
        OpPlanParseResult result = RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"crop","spec":{"x1":"10%"},"aspect":"3:4"}]}""");
        Assert.Null(result.Error);
        Assert.Equal("x1=10% aspect=3:4", Assert.IsType<CropAction>(result.Plan!.Actions[0]).Spec);

        // Beside an EMPTY spec it still satisfies the at-least-one-key rule.
        result = RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"crop","spec":{},"aspect":"1:1"}]}""");
        Assert.Null(result.Error);
        Assert.Equal("aspect=1:1", Assert.IsType<CropAction>(result.Plan!.Actions[0]).Spec);
    }

    [Fact]
    public void Should_Tolerate_An_Identical_Duplicate_Crop_Aspect_But_Fail_A_Conflict()
    {
        OpPlanParseResult duplicate = RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"crop","spec":{"aspect":"4:3"},"aspect":"4:3"}]}""");
        Assert.Null(duplicate.Error);
        Assert.Equal("aspect=4:3", Assert.IsType<CropAction>(duplicate.Plan!.Actions[0]).Spec);

        OpPlanParseResult conflict = RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"crop","spec":{"aspect":"4:3"},"aspect":"3:4"}]}""");
        Assert.Null(conflict.Plan);
        Assert.Contains("different values", conflict.Error);
    }

    // ── filter ──
}
