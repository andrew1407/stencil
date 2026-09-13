using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// What a script's <c>@source</c> may name. The bot has no file system of the user's, so only a
/// sourceless block and an http(s) one run — and that link still faces the §10 echo guard, with the
/// script itself standing in for the chat history it never went through.
/// </summary>
public sealed class ScriptSourceTests : ScriptServiceTestBase
{
    private const string _url = "http://203.0.113.9/a.png";

    [Fact]
    public async Task Should_Let_A_Url_The_Script_Names_Through_The_Echo_Guard()
    {
        _cli.CannedScriptPlan = Plan(ScriptBlock.KIND_URL, _url, $$"""[{"op":"openUrl","url":"{{_url}}"}]""");

        ScriptOutcome outcome = await _service.RunAsync(UserId, $"@source {_url}:\n  @save\n");

        Assert.Equal("Script ran: 1 op.", outcome.Reply);
        Assert.True((await _store.GetAsync(UserId)).HasImage);
    }

    /// <summary>A host the script never wrote is still refused, exactly as in a model turn.</summary>
    [Fact]
    public async Task Should_Refuse_A_Url_The_Script_Never_Wrote()
    {
        _cli.CannedScriptPlan = Plan(ScriptBlock.KIND_URL, _url,
            """[{"op":"openUrl","url":"http://203.0.113.99/a.png"}]""");

        ScriptOutcome outcome = await _service.RunAsync(UserId, $"@source {_url}:\n  @save\n");

        Assert.Contains("never wrote", outcome.Reply);
        Assert.False((await _store.GetAsync(UserId)).HasImage);
    }

    [Theory]
    [InlineData("file")]
    [InlineData("dir")]
    [InlineData("glob")]
    public async Task Should_Refuse_A_Source_Off_The_Local_Disk(string kind)
    {
        await Adopt();
        _cli.CannedScriptPlan = Plan(kind, "shots/", """[{"op":"filter","mode":"bw"}]""");

        ScriptOutcome outcome = await _service.RunAsync(UserId, "@source shots/:\n  @filter bw\n");

        Assert.Contains("no access", outcome.Reply);
        Assert.Contains("shots/", outcome.Reply);
        Assert.Null((await _store.GetAsync(UserId)).Edits.Filter);
    }

    /// <summary>Several blocks each yield their own picture, so they come back as extra renders.</summary>
    [Fact]
    public async Task Should_Return_One_Render_Per_Block_When_A_Script_Has_Several()
    {
        const string second = "http://203.0.113.10/b.png";
        _cli.CannedScriptPlan = new ScriptPlan([], [
            new ScriptBlock(0, _url, ScriptBlock.KIND_URL, [],
                [$$"""[{"op":"openUrl","url":"{{_url}}"},{"op":"filter","mode":"bw"}]"""]),
            new ScriptBlock(1, second, ScriptBlock.KIND_URL, [],
                [$$"""[{"op":"openUrl","url":"{{second}}"},{"op":"filter","mode":"sepia"}]"""]),
        ]);

        ScriptOutcome outcome = await _service.RunAsync(
            UserId, $"@source {_url}:\n @filter bw\n@source {second}:\n @filter sepia\n");

        Assert.Equal(2, outcome.Renders.Count);
        Assert.False(outcome.Mutated); // the album replaces the single working-image render
        Assert.Equal([_url, second], outcome.Renders.Select(r => r.Label));
    }
}
