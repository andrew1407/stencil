using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The op-plan extraction/validation matrix of <c>llm-contract.md</c> §1–3: fence
/// stripping and first-balanced-object extraction, the chat-only fallback, unknown-op
/// skip-with-warning, strict known-op validation (invalid params fail the whole plan), and the
/// shared limits (16 actions / 8 variants / 200 lines / 5000-char string fields).
/// </summary>
public sealed class OpPlanParserTests
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

    [Theory]
    [InlineData("""{"x1":"10%","x2":"-10%","y1":"0","y2":"4cm"}""", "x1=10% x2=-10% y1=0 y2=4cm")]
    [InlineData("""{"y2":"12.5px"}""", "y2=12.5px")]
    [InlineData("""{"x1":"3in"}""", "x1=3in")]
    public void CropJoinsValidatedTokensWithSpaces(string spec, string expected)
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
    public void CropRejectsBadSpecs(string spec)
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"ok","actions":[{"op":"crop","spec":{{spec}}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("crop", result.Error);
    }

    [Fact]
    public void CropAcceptsAnAspectKeyAndKeepsItsValue()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"crop","spec":{"x1":"10%","aspect":"4:3"}}]}""");

        Assert.Null(result.Error);
        Assert.Equal("x1=10% aspect=4:3", Assert.IsType<CropAction>(result.Plan!.Actions[0]).Spec);
    }

    [Fact]
    public void CropAcceptsAnAspectOnlySpec()
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
    public void CropRejectsMalformedAspects(string aspect)
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            $$$"""{"reply":"ok","actions":[{"op":"crop","spec":{"x1":"10%","aspect":"{{{aspect}}}"}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("crop", result.Error);
    }

    [Fact]
    public void CropAspectBesideTheSpecIsFoldedIn()
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
    public void CropAspectIdenticalDuplicateIsToleratedButAConflictFails()
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
    public void CropRejectsMalformedAspectsBesideTheSpec(string aspect)
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"ok","actions":[{"op":"crop","spec":{"x1":"10%"},"aspect":{{aspect}}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("crop", result.Error);
    }

    // ── filter ──

    [Fact]
    public void FilterCustomRequiresATintAndOthersForbidIt()
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
    public void LayoutLinesGetTheSharedDefaultsForOmittedFields()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"layout","lines":[{"points":[{"x":1,"y":2},{"x":3,"y":4}]}]}]}""");

        Assert.Null(result.Error);
        LayoutLine line = Assert.Single(Assert.IsType<LayoutAction>(result.Plan!.Actions[0]).Lines);
        Assert.Equal(LayoutLine.DefaultColor, line.Color);
        Assert.Equal(LayoutLine.DefaultThickness, line.Thickness);
        Assert.Equal(LayoutLine.DefaultStyle, line.Style);
        Assert.Equal(LayoutLine.DefaultFillColor, line.FillColor);
        Assert.Equal(2, line.Points.Count);
        Assert.Equal(3, line.Points[1].X);
    }

    [Fact]
    public void LayoutRejectsBadLines()
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
    public void LayoutRejectsMoreThan200Lines()
    {
        string lines = string.Join(',', Enumerable.Repeat("""{"points":[{"x":0,"y":0}]}""", 201));
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"ok","actions":[{"op":"layout","lines":[{{lines}}]}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("200", result.Error);
    }

    // ── formula ──

    [Fact]
    public void FormulaEnforcesCharsetAndTheAxisVariable()
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
    public void FormulaLongerThan5000CharsFailsThePlan()
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
    public void PageValidatesTheIsoFormatName(string format, bool ok)
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"ok","actions":[{"op":"page","format":"{{format}}"}]}""");
        Assert.Equal(ok, result.Error is null);
    }

    [Fact]
    public void BlankTakesHexOrCssNameColoursAndAnOptionalFormat()
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
    public void FrameTakesExactlyOneOfIndexOrIndices()
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
    public void FrameRejectsMoreThan32Indices()
    {
        string indices = string.Join(',', Enumerable.Range(0, 33));
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"ok","actions":[{"op":"frame","indices":[{{indices}}]}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("32", result.Error);
    }

    // ── limits ──

    [Fact]
    public void MoreThan16ActionsFailsThePlan()
    {
        string actions = string.Join(',', Enumerable.Repeat("""{"op":"rotate","dir":"left"}""", 17));
        OpPlanParseResult result = OpPlanParser.Parse($$"""{"reply":"ok","actions":[{{actions}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("16", result.Error);
    }

    [Fact]
    public void MoreThan8VariantsFailsThePlan()
    {
        string variants = string.Join(',', Enumerable.Repeat("""{"label":"v","actions":[]}""", 9));
        OpPlanParseResult result = OpPlanParser.Parse($$"""{"reply":"ok","variants":[{{variants}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("8", result.Error);
    }

    [Fact]
    public void MoreThan16ActionsInsideAVariantFailsThePlan()
    {
        string actions = string.Join(',', Enumerable.Repeat("""{"op":"rotate","dir":"left"}""", 17));
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"ok","variants":[{"label":"v","actions":[{{actions}}]}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("16", result.Error);
    }

    // ── §2.1 multi-image ops: `image` switches to a turn attachment, `save` persists ──

    [Fact]
    public void ParsesImageAndSaveWithTheirOptionalName()
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
    public void ImageIndexMustBeAWholeAttachmentNumber(string action)
    {
        OpPlanParseResult result = OpPlanParser.Parse($$"""{"reply":"x","actions":[{{action}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("image", result.Error);
    }

    [Fact]
    public void SaveNameIsBoundedTo120Characters()
    {
        string name = new('x', 121);
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"x","actions":[{"op":"save","name":"{{name}}"}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("120", result.Error);
    }

    [Fact]
    public void SavePathIsTrimmedAndEmptyAfterTrimIsAbsent()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"x","actions":[{"op":"save","name":"p","path":"  ~/Downloads  "},{"op":"save","path":"   "}]}""");
        Assert.Null(result.Error);
        Assert.Equal("~/Downloads", Assert.IsType<SaveAction>(result.Plan!.Actions[0]).Path);
        Assert.Null(Assert.IsType<SaveAction>(result.Plan.Actions[1]).Path);
    }

    [Fact]
    public void SavePathIsBoundedTo1024Characters()
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
    public void SavePathMustBeALocalPathString(string action)
    {
        OpPlanParseResult result = OpPlanParser.Parse($$"""{"reply":"x","actions":[{{action}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("path", result.Error);
    }

    [Theory]
    [InlineData("""{"op":"image","index":1}""")]
    [InlineData("""{"op":"save"}""")]
    public void ImageAndSaveInsideAVariantOrPreviewCostThatVariantOnly(string action)
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

    [Fact]
    public void ConnectAndDisconnectParseWithANonEmptyServerAndNothingElse()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """
            {"reply":"ok","actions":[
              {"op":"connect","server":"http://srv:8090"},{"op":"disconnect","server":"srv"}]}
            """);

        Assert.Null(result.Error);
        Assert.Empty(result.Warnings);
        Assert.Equal("http://srv:8090", Assert.IsType<ConnectAction>(result.Plan!.Actions[0]).Server);
        Assert.Equal("srv", Assert.IsType<DisconnectAction>(result.Plan.Actions[1]).Server);
    }

    [Theory]
    [InlineData("""{"op":"connect"}""")]
    [InlineData("""{"op":"connect","server":"   "}""")]
    [InlineData("""{"op":"connect","server":42}""")]
    [InlineData("""{"op":"connect","server":"srv","token":"t"}""")] // plans never carry tokens
    [InlineData("""{"op":"disconnect","server":""}""")]
    [InlineData("""{"op":"disconnect","server":"srv","url":"http://srv"}""")]
    public void ConnectionOpsRejectABadServerAndAnyExtraFieldOutright(string action)
    {
        OpPlanParseResult result = OpPlanParser.Parse($$"""{"reply":"x","actions":[{{action}}]}""");
        Assert.Null(result.Plan);
        Assert.NotNull(result.Error);
    }

    [Theory]
    [InlineData("connect")]
    [InlineData("disconnect")]
    public void ConnectionOpsInsideAVariantDropThatVariant(string op)
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            $$"""{"reply":"x","variants":[{"label":"v","actions":[{"op":"{{op}}","server":"srv"}]}]}""");
        Assert.Null(result.Error);
        Assert.Empty(result.Plan!.Variants);
        string warning = Assert.Single(result.Warnings);
        Assert.Contains("§10", warning);
        Assert.Contains("variant", warning);
    }

    // ── §2 undo / redo / reset ──

    [Fact]
    public void UndoAndRedoTakeAnOptionalBoundedStepsCount()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"undo"},{"op":"redo","steps":5},{"op":"undo","steps":20}]}""");

        Assert.Null(result.Error);
        Assert.Equal(1, Assert.IsType<UndoAction>(result.Plan!.Actions[0]).Steps);
        Assert.Equal(5, Assert.IsType<RedoAction>(result.Plan.Actions[1]).Steps);
        Assert.Equal(20, Assert.IsType<UndoAction>(result.Plan.Actions[2]).Steps);
    }

    [Theory]
    [InlineData("""{"op":"undo","steps":0}""")]
    [InlineData("""{"op":"undo","steps":21}""")]
    [InlineData("""{"op":"redo","steps":1.5}""")]
    [InlineData("""{"op":"redo","steps":"2"}""")]
    [InlineData("""{"op":"undo","count":2}""")]
    public void UndoRedoRejectBadSteps(string action)
    {
        OpPlanParseResult result = OpPlanParser.Parse($$"""{"reply":"x","actions":[{{action}}]}""");
        Assert.Null(result.Plan);
        Assert.NotNull(result.Error);
    }

    [Fact]
    public void ResetClearAndClearChatTakeNoFieldsAtAll()
    {
        OpPlanParseResult ok = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"reset"},{"op":"clear"},{"op":"clearChat"}]}""");
        Assert.Null(ok.Error);
        Assert.IsType<ResetAction>(ok.Plan!.Actions[0]);
        Assert.IsType<ClearAction>(ok.Plan.Actions[1]);
        Assert.IsType<ClearChatAction>(ok.Plan.Actions[2]);

        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"reset","hard":true}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"clear","lines":true}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"clearChat","confirm":true}]}""").Error);
    }

    // ── §10 lineStyle ──

    [Fact]
    public void LineStyleTakesAnySubsetOfThePenFields()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """
            {"reply":"ok","actions":[{"op":"lineStyle","color":"#00ff00","thickness":3,
              "pointSize":6,"style":"dashed","fillColor":"transparent","pointColor":"","drawMode":"rect"}]}
            """);

        Assert.Null(result.Error);
        LineStyleAction pen = Assert.IsType<LineStyleAction>(Assert.Single(result.Plan!.Actions));
        Assert.Equal("#00ff00", pen.Color);
        Assert.Equal(3, pen.Thickness);
        Assert.Equal(6, pen.PointSize);
        Assert.Equal("dashed", pen.Style);
        Assert.Equal("transparent", pen.FillColor);
        Assert.Equal("", pen.PointColor);
        Assert.Equal("rect", pen.DrawMode);

        // A CSS colour name works like the /color command's.
        OpPlanParseResult named = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"lineStyle","color":"tomato"}]}""");
        Assert.Null(named.Error);
        Assert.Equal("tomato", Assert.IsType<LineStyleAction>(named.Plan!.Actions[0]).Color);
    }

    [Theory]
    [InlineData("""{"op":"lineStyle"}""")]                          // at least one field
    [InlineData("""{"op":"lineStyle","thickness":0}""")]            // 1..20
    [InlineData("""{"op":"lineStyle","thickness":21}""")]
    [InlineData("""{"op":"lineStyle","pointSize":31}""")]           // 1..30
    [InlineData("""{"op":"lineStyle","style":"wavy"}""")]
    [InlineData("""{"op":"lineStyle","drawMode":"circle"}""")]
    [InlineData("""{"op":"lineStyle","fillColor":"red"}""")]        // hex or "transparent" only
    [InlineData("""{"op":"lineStyle","color":"#12"}""")]
    [InlineData("""{"op":"lineStyle","pointColor":"red"}""")]       // hex or "" only
    [InlineData("""{"op":"lineStyle","width":2}""")]                // unknown field
    public void LineStyleRejectsBadFields(string action)
    {
        OpPlanParseResult result = OpPlanParser.Parse($$"""{"reply":"x","actions":[{{action}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("lineStyle", result.Error);
    }

    // ── §10 openUrl ──

    [Fact]
    public void OpenUrlParsesAnHttpUrlTrimmedWithAnOptionalIncognito()
    {
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"openUrl","url":" https://pics.example/cat.png ","incognito":true}]}""");

        Assert.Null(result.Error);
        OpenUrlAction open = Assert.IsType<OpenUrlAction>(Assert.Single(result.Plan!.Actions));
        Assert.Equal("https://pics.example/cat.png", open.Url);
        Assert.True(open.Incognito);
    }

    [Theory]
    [InlineData("""{"op":"openUrl"}""")]
    [InlineData("""{"op":"openUrl","url":"ftp://x/a.png"}""")]
    [InlineData("""{"op":"openUrl","url":"not a url"}""")]
    [InlineData("""{"op":"openUrl","url":"http://"}""")]
    [InlineData("""{"op":"openUrl","url":"http://a b/c.png"}""")]
    [InlineData("""{"op":"openUrl","url":"https://x/a.png","incognito":"yes"}""")]
    [InlineData("""{"op":"openUrl","url":"https://x/a.png","cookies":true}""")]
    public void OpenUrlRejectsNonHttpShapes(string action)
    {
        OpPlanParseResult result = OpPlanParser.Parse($$"""{"reply":"x","actions":[{{action}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("openUrl", result.Error);
    }

    // ── §10 renameProject / describe / blankColor / projectColor / export ──

    [Fact]
    public void RenameProjectTrimsAndBoundsTheName()
    {
        OpPlanParseResult ok = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"renameProject","name":" Poster draft "}]}""");
        Assert.Null(ok.Error);
        Assert.Equal("Poster draft", Assert.IsType<RenameProjectAction>(ok.Plan!.Actions[0]).Name);

        Assert.NotNull(OpPlanParser.Parse(
            """{"reply":"x","actions":[{"op":"renameProject","name":"   "}]}""").Error);
        string over = new('x', 81);
        Assert.NotNull(OpPlanParser.Parse(
            $$"""{"reply":"x","actions":[{"op":"renameProject","name":"{{over}}"}]}""").Error);
    }

    [Fact]
    public void DescribeTakesUpTo500CharsAndEmptyClears()
    {
        OpPlanParseResult empty = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"describe","text":""}]}""");
        Assert.Null(empty.Error);
        Assert.Equal("", Assert.IsType<DescribeAction>(empty.Plan!.Actions[0]).Text);

        string max = new('d', 500);
        Assert.Null(OpPlanParser.Parse(
            $$"""{"reply":"ok","actions":[{"op":"describe","text":"{{max}}"}]}""").Error);
        string over = new('d', 501);
        OpPlanParseResult tooLong = OpPlanParser.Parse(
            $$"""{"reply":"x","actions":[{"op":"describe","text":"{{over}}"}]}""");
        Assert.Null(tooLong.Plan);
        Assert.Contains("500", tooLong.Error);
        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"describe"}]}""").Error);
    }

    [Fact]
    public void BlankColorAndProjectColorValidateTheirColours()
    {
        OpPlanParseResult ok = OpPlanParser.Parse(
            """
            {"reply":"ok","actions":[{"op":"blankColor","color":"lavender"},
              {"op":"projectColor","color":"#ec4899"},{"op":"projectColor","color":""}]}
            """);
        Assert.Null(ok.Error);
        Assert.Equal("lavender", Assert.IsType<BlankColorAction>(ok.Plan!.Actions[0]).Color);
        Assert.Equal("#ec4899", Assert.IsType<ProjectColorAction>(ok.Plan.Actions[1]).Color);
        Assert.Equal("", Assert.IsType<ProjectColorAction>(ok.Plan.Actions[2]).Color);

        // blankColor takes hex/CSS names; projectColor only hex or "" (the clear).
        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"blankColor","color":"#12345"}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"blankColor"}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"projectColor","color":"pink"}]}""").Error);
    }

    [Fact]
    public void ExportTakesLayoutOrProjectOnly()
    {
        OpPlanParseResult ok = OpPlanParser.Parse(
            """{"reply":"ok","actions":[{"op":"export","what":"layout"},{"op":"export","what":"project"}]}""");
        Assert.Null(ok.Error);
        Assert.Equal("layout", Assert.IsType<ExportAction>(ok.Plan!.Actions[0]).What);
        Assert.Equal("project", Assert.IsType<ExportAction>(ok.Plan.Actions[1]).What);

        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"export","what":"video"}]}""").Error);
        Assert.NotNull(OpPlanParser.Parse("""{"reply":"x","actions":[{"op":"export"}]}""").Error);
    }

    // ── §2 widened forms: page custom dims, blank cm dims, formula enabled/empty ──

    [Fact]
    public void PageTakesAFormatOrCustomCmDimsButNeverBoth()
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
    public void BlankTakesOptionalCmDimsBothOrNeither()
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
    public void FormulaEnabledRidesAloneAndEmptyExprIsAllowed()
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
    public void HugeInvalidValuesAreClippedInTheErrorMessage()
    {
        string token = new('x', 500);
        OpPlanParseResult result = OpPlanParser.Parse(
            $$$"""{"reply":"ok","actions":[{"op":"crop","spec":{"x1":"{{{token}}}"}}]}""");
        Assert.Null(result.Plan);
        Assert.Contains("…", result.Error);
        Assert.True(result.Error!.Length < 150); // the 500-char token was clipped, not echoed
    }
}
