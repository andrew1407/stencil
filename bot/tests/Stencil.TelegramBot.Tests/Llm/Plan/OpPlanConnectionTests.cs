using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Application.Llm.Plan;

namespace Stencil.TelegramBot.Tests.Llm.Plan;

/// <summary>§10 <c>connect</c>/<c>disconnect</c> and the no-field ops (<c>reset</c>, <c>clear</c>, <c>clearChat</c>): a non-empty server and nothing else.</summary>
public sealed class OpPlanConnectionTests
{
    [Fact]
    public void Should_Parse_Connect_And_Disconnect_With_A_Non_Empty_Server_And_Nothing_Else()
    {
        OpPlanParseResult result = RecordedPlans.Parse(
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
    [InlineData("connect")]
    [InlineData("disconnect")]
    public void Should_Drop_The_Variant_For_Connection_Ops_Inside_It(string op)
    {
        OpPlanParseResult result = RecordedPlans.Parse(
            $$"""{"reply":"x","variants":[{"label":"v","actions":[{"op":"{{op}}","server":"srv"}]}]}""");
        Assert.Null(result.Error);
        Assert.Empty(result.Plan!.Variants);
        string warning = Assert.Single(result.Warnings);
        Assert.Contains($"editor-settings op \"{op}\"", warning);
        Assert.Contains("variant", warning);
    }

    // ── §2 undo / redo / reset ──

    [Fact]
    public void Should_Take_An_Optional_Bounded_Steps_Count_For_Undo_And_Redo()
    {
        OpPlanParseResult result = RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"undo"},{"op":"redo","steps":5},{"op":"undo","steps":20}]}""");

        Assert.Null(result.Error);
        Assert.Equal(1, Assert.IsType<UndoAction>(result.Plan!.Actions[0]).Steps);
        Assert.Equal(5, Assert.IsType<RedoAction>(result.Plan.Actions[1]).Steps);
        Assert.Equal(20, Assert.IsType<UndoAction>(result.Plan.Actions[2]).Steps);
    }

    [Fact]
    public void Should_Take_No_Fields_At_All_For_Reset_Clear_And_Clear_Chat()
    {
        OpPlanParseResult ok = RecordedPlans.Parse(
            """{"reply":"ok","actions":[{"op":"reset"},{"op":"clear"},{"op":"clearChat"}]}""");
        Assert.Null(ok.Error);
        Assert.IsType<ResetAction>(ok.Plan!.Actions[0]);
        Assert.IsType<ClearAction>(ok.Plan.Actions[1]);
        Assert.IsType<ClearChatAction>(ok.Plan.Actions[2]);

        Assert.NotNull(RecordedPlans.Parse("""{"reply":"x","actions":[{"op":"reset","hard":true}]}""").Error);
        Assert.NotNull(RecordedPlans.Parse("""{"reply":"x","actions":[{"op":"clear","lines":true}]}""").Error);
        Assert.NotNull(RecordedPlans.Parse("""{"reply":"x","actions":[{"op":"clearChat","confirm":true}]}""").Error);
    }

    // ── §10 lineStyle ──
}
