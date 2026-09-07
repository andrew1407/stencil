using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The §11 interactive-reply card as the bot sees it: strict parsing (a card nobody can answer
/// is a plan error; an image reference is validated like everywhere else — exactly one of
/// url / projectId / scanIndex, http(s) urls only), the bot's own preview rule (it resolves no
/// option picture at all — a render spec, a project id, a page-scan index and a URL alike keep
/// the option and lose the picture), and the answer text a tap composes for the next turn.
/// </summary>
public sealed class AskCardTests
{
    private static OpPlan Parse(string json)
    {
        OpPlanParseResult result = OpPlanParser.Parse(json);
        Assert.Null(result.Error);
        Assert.NotNull(result.Plan);
        return result.Plan!;
    }

    private static OpPlanParseResult Reject(string json)
    {
        OpPlanParseResult result = OpPlanParser.Parse(json);
        Assert.NotNull(result.Error);
        Assert.Null(result.Plan);
        return result;
    }

    [Fact]
    public void ParsesACardWithItsDefaults()
    {
        OpPlan plan = Parse("""
            {"version":1,"reply":"pick","ask":{"question":"Which tint?","options":[{"label":"Sepia"},{"label":"B&W"}]}}
            """);
        Assert.NotNull(plan.Ask);
        AskCard ask = plan.Ask!;
        Assert.Equal("Which tint?", ask.Question);
        Assert.False(ask.Multi);
        Assert.False(ask.AllowCustom);
        Assert.Equal(OpPlanParser.DefaultCustomLabel, ask.CustomLabel);
        Assert.Equal(["Sepia", "B&W"], ask.Options.Select(o => o.Label));
    }

    [Fact]
    public void MultiModeCustomRowAndTrimming()
    {
        OpPlan plan = Parse("""
            {"version":1,"reply":"pick","ask":{"question":"  Which?  ","mode":"multi","allowCustom":true,"customLabel":" Other ","options":[{"label":"  A  "},{"label":"B"}]}}
            """);
        AskCard ask = plan.Ask!;
        Assert.True(ask.Multi);
        Assert.True(ask.AllowCustom);
        Assert.Equal("Which?", ask.Question);
        Assert.Equal("Other", ask.CustomLabel);
        Assert.Equal("A", ask.Options[0].Label);
    }

    [Fact]
    public void NoCardOnAnOrdinaryOrChatOnlyTurn()
    {
        Assert.Null(Parse("""{"version":1,"reply":"hi","actions":[]}""").Ask);
        Assert.Null(Parse("just chatting").Ask);
    }

    // Never handed to Telegram to fetch: nobody chose that host. The option survives by
    // label with a warning, like any other reference this chat cannot resolve.
    [Fact]
    public void AnImageUrlIsNotFetchedAndTheOptionKeepsItsLabel()
    {
        OpPlanParseResult result = OpPlanParser.Parse("""
            {"version":1,"reply":"pick","ask":{"question":"Which?","options":[{"label":"Web","image":{"url":"https://example.com/cat.jpg"}},{"label":"Plain"}]}}
            """);
        Assert.Null(result.Error);
        Assert.Equal(["Web", "Plain"], result.Plan!.Ask!.Options.Select(o => o.Label));
        Assert.Contains(result.Warnings, w => w.Contains("does not fetch"));
    }

    [Theory]
    [InlineData("""{"scanIndex":2}""")]
    [InlineData("""{"projectId":"p_12"}""")]
    [InlineData("""{"url":"https://example.com/cat.jpg"}""")]
    [InlineData("""{"url":"http://169.254.169.254/latest/meta-data/"}""")]
    public void AReferenceTheChatCannotResolveKeepsTheOptionButLosesThePicture(string image)
    {
        // Built by concatenation: the JSON's own braces fight raw-string interpolation here.
        OpPlanParseResult result = OpPlanParser.Parse(
            """{"version":1,"reply":"pick","ask":{"question":"Which?","options":[{"label":"Elsewhere","image":"""
            + image
            + """},{"label":"Plain"}]}}""");
        Assert.Null(result.Error);
        Assert.Equal(2, result.Plan!.Ask!.Options.Count);
        Assert.Equal("Elsewhere", result.Plan.Ask.Options[0].Label);
        Assert.NotEmpty(result.Warnings);
    }

    [Fact]
    public void ARenderPreviewIsDroppedWithOneWarningNotTheOption()
    {
        OpPlanParseResult result = OpPlanParser.Parse("""
            {"version":1,"reply":"pick","ask":{"question":"Which tint?","options":[{"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]},{"label":"B&W","actions":[{"op":"filter","mode":"bw"}]}]}}
            """);
        Assert.Null(result.Error);
        Assert.Equal(["Sepia", "B&W"], result.Plan!.Ask!.Options.Select(o => o.Label));
        Assert.Single(result.Warnings, w => w.Contains("rendered preview"));
    }

    // §1's one leniency, ask-side: the misplaced op costs the PREVIEW, not the option — and
    // never the plan the user actually asked for.
    [Fact]
    public void APreviewCarryingAMisplacedOpLosesThePreviewOnlyAndSaysWhich()
    {
        OpPlanParseResult result = OpPlanParser.Parse("""
            {"version":1,"reply":"pick","actions":[{"op":"filter","mode":"bw"}],"ask":{"question":"Which?","options":[{"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]},{"label":"Wiped","actions":[{"op":"clear"}]}]}}
            """);

        Assert.Null(result.Error);
        Assert.Single(result.Plan!.Actions);
        Assert.Equal(["Sepia", "Wiped"], result.Plan.Ask!.Options.Select(o => o.Label));
        string dropped = Assert.Single(result.Warnings, w => w.Contains("\"clear\""));
        Assert.Contains("option 2", dropped);
        Assert.Contains("Wiped", dropped);
        Assert.Single(result.Warnings, w => w.Contains("rendered preview"));   // the Sepia one
    }

    [Theory]
    [InlineData("""{"version":1,"reply":"x","ask":"hello"}""")]                                                    // not an object
    [InlineData("""{"version":1,"reply":"x","ask":{"options":[{"label":"A"},{"label":"B"}]}}""")]                  // no question
    [InlineData("""{"version":1,"reply":"x","ask":{"question":"  ","options":[{"label":"A"},{"label":"B"}]}}""")]   // blank question
    [InlineData("""{"version":1,"reply":"x","ask":{"question":"Q","options":[]}}""")]                              // 0 options
    [InlineData("""{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"only"}]}}""")]              // 1 option
    [InlineData("""{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"1"},{"label":"2"},{"label":"3"},{"label":"4"},{"label":"5"},{"label":"6"}]}}""")]
    [InlineData("""{"version":1,"reply":"x","ask":{"question":"Q","mode":"maybe","options":[{"label":"A"},{"label":"B"}]}}""")]
    [InlineData("""{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A"},{"label":"B"}],"sneaky":1}}""")]
    [InlineData("""{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","sneaky":1},{"label":"B"}]}}""")]
    [InlineData("""{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":""},{"label":"B"}]}}""")]
    [InlineData("""{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","actions":[],"image":{"url":"https://e/x"}},{"label":"B"}]}}""")]
    [InlineData("""{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","image":{}},{"label":"B"}]}}""")]
    [InlineData("""{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","image":{"url":"https://e/x","projectId":"p"}},{"label":"B"}]}}""")]
    [InlineData("""{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","image":{"url":"data:image/png;base64,AA"}},{"label":"B"}]}}""")]   // http(s) only (§11.1)
    [InlineData("""{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","image":{"url":"file:///etc/passwd"}},{"label":"B"}]}}""")]
    [InlineData("""{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","image":{"scanIndex":"2"}},{"label":"B"}]}}""")]
    [InlineData("""{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A"},{"label":"B"}],"allowCustom":"yes"}}""")]
    public void MalformedCardsRejectTheWholePlan(string json) => Reject(json);

    [Fact]
    public void FiveOptionsIsTheCapAndItParses()
    {
        OpPlan plan = Parse("""
            {"version":1,"reply":"pick","ask":{"question":"Q","options":[{"label":"1"},{"label":"2"},{"label":"3"},{"label":"4"},{"label":"5"}]}}
            """);
        Assert.Equal(OpPlanParser.MaxAskOptions, plan.Ask!.Options.Count);
    }

    [Fact]
    public void AnswerTextJoinsThePicksOrTakesTheTypedText()
    {
        Assert.Equal("Sepia", OpPlanParser.AskAnswerText(["Sepia"]));
        Assert.Equal("Sepia, B&W", OpPlanParser.AskAnswerText(["Sepia", "B&W"]));
        Assert.Equal("", OpPlanParser.AskAnswerText([]));
        Assert.Equal("a warm green", OpPlanParser.AskAnswerText(["Sepia"], "  a warm green  "));
        Assert.Equal(OpPlanParser.MaxAskAnswer, OpPlanParser.AskAnswerText([], new string('x', OpPlanParser.MaxAskAnswer + 20)).Length);
        Assert.Equal("A", OpPlanParser.AskAnswerText(["A", "   ", ""]));   // blanks never pad the answer
    }
}
