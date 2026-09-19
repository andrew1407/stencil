using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>§3.2 <c>crop</c>: the spec grammar, the aspect key inside and beside the spec, and the fold that tolerates the duplicate but refuses a conflict.</summary>
public sealed class OpPlanCropTests
{
    [Theory]
    [InlineData("""{"x1":"10%","x2":"-10%","y1":"0","y2":"4cm"}""", "x1=10% x2=-10% y1=0 y2=4cm")]
    [InlineData("""{"y2":"12.5px"}""", "y2=12.5px")]
    [InlineData("""{"x1":"3in"}""", "x1=3in")]
    public void Should_Join_Validated_Tokens_With_Spaces_For_Crop(string spec, string expected)
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"ok","actions":[{"op":"crop","spec":{{spec}}}]}""");

        Assert.Null(result.Error);
        Assert.Equal(expected, Assert.IsType<CropAction>(result.Plan!.Actions[0]).Spec);
    }

    [Theory]
    [InlineData("""{"z1":"10%"}""")]     // key outside x1/x2/y1/y2/aspect
    [InlineData("""{}""")]               // no keys at all
    [InlineData("""{"x1":"ten"}""")]     // not a token
    [InlineData("""{"x1":"10 %"}""")]    // inner space
    [InlineData("""{"x1":"10km"}""")]    // unknown unit
    [InlineData("""{"x1":10}""")]        // not a string
    public void Should_Reject_Bad_Specs_For_Crop(string spec)
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"ok","actions":[{"op":"crop","spec":{{spec}}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("crop", result.Error);
    }

    [Fact]
    public void Should_Accept_An_Aspect_Key_And_Keep_Its_Value_For_Crop()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"crop","spec":{"x1":"10%","aspect":"4:3"}}]}""");

        Assert.Null(result.Error);
        Assert.Equal("x1=10% aspect=4:3", Assert.IsType<CropAction>(result.Plan!.Actions[0]).Spec);
    }

    [Fact]
    public void Should_Accept_An_Aspect_Only_Spec_For_Crop()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"crop","spec":{"aspect":"16:9"}}]}""");

        Assert.Null(result.Error);
        Assert.Equal("aspect=16:9", Assert.IsType<CropAction>(result.Plan!.Actions[0]).Spec);
    }

    [Theory]
    [InlineData("0:3")]      // zero width
    [InlineData("4:0")]      // zero height
    [InlineData("-4:3")]     // signed
    [InlineData("4:-3")]
    [InlineData("4")]        // no colon
    [InlineData("4:")]       // missing part
    [InlineData(":3")]
    [InlineData("4:3:2")]    // extra colon
    [InlineData("a:b")]      // not digits
    [InlineData("4.5:3")]    // not integers
    [InlineData("1e2:3")]    // exponent
    [InlineData("")]         // empty
    public void Should_Reject_Malformed_Aspects_For_Crop(string aspect)
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            $$$"""{"reply":"ok","actions":[{"op":"crop","spec":{"x1":"10%","aspect":"{{{aspect}}}"}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("crop", result.Error);
    }

    [Fact]
    public void Should_Fold_In_A_Crop_Aspect_Beside_The_Spec()
    {
        // §1 tolerance: models sometimes emit "aspect" beside "spec" — same validation, folded.
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"crop","spec":{"x1":"10%"},"aspect":"3:4"}]}""");
        Assert.Null(result.Error);
        Assert.Equal("x1=10% aspect=3:4", Assert.IsType<CropAction>(result.Plan!.Actions[0]).Spec);

        // Beside an EMPTY spec it still satisfies the at-least-one-key rule.
        result = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"crop","spec":{},"aspect":"1:1"}]}""");
        Assert.Null(result.Error);
        Assert.Equal("aspect=1:1", Assert.IsType<CropAction>(result.Plan!.Actions[0]).Spec);
    }

    [Fact]
    public void Should_Tolerate_An_Identical_Duplicate_Crop_Aspect_But_Fail_A_Conflict()
    {
        OpPlanParseResult duplicate = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"crop","spec":{"aspect":"4:3"},"aspect":"4:3"}]}""");
        Assert.Null(duplicate.Error);
        Assert.Equal("aspect=4:3", Assert.IsType<CropAction>(duplicate.Plan!.Actions[0]).Spec);

        OpPlanParseResult conflict = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"crop","spec":{"aspect":"4:3"},"aspect":"3:4"}]}""");
        Assert.Null(conflict.Plan);
        Assert.Contains("conflicting", conflict.Error);
    }

    [Theory]
    [InlineData("\"1.5:2\"")]   // not integers
    [InlineData("\"4:\"")]      // missing part
    [InlineData("43")]          // not a string
    public void Should_Reject_Malformed_Aspects_Beside_The_Spec_For_Crop(string aspect)
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"ok","actions":[{"op":"crop","spec":{"x1":"10%"},"aspect":{{aspect}}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("crop", result.Error);
    }

    // ── filter ──
}
