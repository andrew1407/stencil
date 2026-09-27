using Stencil.TelegramBot.Application.Llm.Plan;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Tests.Llm.Plan;

/// <summary>The typed mapper over hand-written result documents (cli/CONTRACT.md §7): core's canonical <c>message</c> passes through, and what only the typed actions can refuse reads in core's frame. Offline: no CLI.</summary>
public sealed class OpPlanMapperTests
{
    [Fact]
    public void Should_Show_Cores_Own_Message_For_A_Failed_Plan()
    {
        const string result = """{"status":"invalid","reply":"","actions":[],"variants":[],"ask":null,"warnings":[],"error":{"code":"E_PLAN","rule":"tooMany","scope":"actions","max":16,"detail":"more than 16 actions in \"actions\"","message":"Invalid plan: more than 16 actions in \"actions\""}}""";

        OpPlanParseResult mapped = OpPlanParser.Map(result);

        Assert.Null(mapped.Plan);
        Assert.Equal("Invalid plan: more than 16 actions in \"actions\"", mapped.Error);
    }

    /// <summary>Core admits any finite integer; an index past 32 bits fails the plan rather than wrapping.</summary>
    [Fact]
    public void Should_Fail_A_Plan_Whose_Integer_Cannot_Fit_The_Typed_Action()
    {
        OpPlanParseResult result = OpPlanParser.Map(valid("""{"op":"frame","index":4294967296}"""));

        Assert.Null(result.Plan);
        Assert.Equal("Invalid frame action: \"index\" must fit a 32-bit integer", result.Error);
    }

    /// <summary>A CLI built from a newer registry may pass an op this build cannot run: §1's skip, never a crash.</summary>
    [Fact]
    public void Should_Skip_An_Op_The_Bot_Cannot_Map_With_The_Unknown_Op_Note()
    {
        OpPlanParseResult result = OpPlanParser.Map(valid("""{"op":"sparkle"},{"op":"rotate","dir":"left","times":1}"""));

        Assert.Null(result.Error);
        Assert.IsType<RotateAction>(Assert.Single(result.Plan!.Actions));
        Assert.Equal("Skipped unknown operation \"sparkle\"", Assert.Single(result.Warnings));
    }

    /// <summary>A script chunk has no reply by design, so core's reply-omitted note is not repeated to the user.</summary>
    [Fact]
    public void Should_Drop_Only_The_Reply_Omitted_Note_For_A_Script_Chunk()
    {
        const string result = """{"status":"valid","reply":"Done.","actions":[{"op":"filter","mode":"bw"}],"variants":[],"ask":null,"warnings":[{"code":"W_REPLY_OMITTED","message":"The model omitted its reply — the plan still ran"}],"error":null}""";

        Assert.Empty(OpPlanParser.MapScriptChunk(result).Warnings);
        Assert.Equal(["The model omitted its reply — the plan still ran"], OpPlanParser.Map(result).Warnings);
    }

    [Theory]
    [InlineData("not json")]
    [InlineData("""{"status":"valid"}""")]
    [InlineData("""{"status":"valid","reply":"x","actions":{},"variants":[],"ask":null,"warnings":[],"error":null}""")]
    [InlineData("""{"status":"invalid","reply":"","actions":[],"variants":[],"ask":null,"warnings":[],"error":{"code":"E_PLAN","detail":"x"}}""")]
    [InlineData("""{"status":"valid","reply":"x","actions":[],"variants":[],"ask":null,"warnings":[{"code":"W_UNKNOWN_OP","op":"z"}],"error":null}""")]
    public void Should_Report_A_Result_That_Breaks_The_Contract_As_A_Cli_Failure(string result) =>
        Assert.Throws<StencilCliException>(() => OpPlanParser.Map(result));

    private static string valid(string actions) =>
        $$"""{"status":"valid","reply":"ok","actions":[{{actions}}],"variants":[],"ask":null,"warnings":[],"error":null}""";
}
