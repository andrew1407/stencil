using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The <c>.stc</c> path inside the Application ring: the script reaches the CLI as a temp file,
/// the lowered actions run through the SAME op-plan validator and executor a model plan does, and
/// the temp file never outlives the call.
/// </summary>
public sealed class ScriptServiceTests : ScriptServiceTestBase
{
    [Fact]
    public async Task Should_Apply_The_Lowered_Actions_To_The_Working_Image()
    {
        await Adopt();
        _cli.CannedScriptPlan = Plan(ScriptBlock.KIND_PROJECT, "",
            """[{"op":"crop","spec":{"x1":"10%","x2":"90%"}},{"op":"filter","mode":"bw"}]""");

        ScriptOutcome outcome = await _service.RunAsync(UserId, "@crop 10%\n@filter bw\n");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("x1=10% x2=90%", session.Edits.CropSpec);
        Assert.Equal("bw", session.Edits.Filter);
        Assert.Equal("Script ran: 2 ops.", outcome.Reply);
        Assert.True(outcome.Mutated);
    }

    /// <summary>The script reaches the CLI as a file beside the user's workspace, and is deleted.</summary>
    [Fact]
    public async Task Should_Hand_The_Script_Over_As_A_Temp_File_And_Delete_It()
    {
        await Adopt();

        await _service.RunAsync(UserId, "@filter bw\n");

        string path = _cli.LastScriptCall!.Value.Script;
        Assert.Equal("@filter bw\n", Assert.Single(_cli.ScriptTexts));
        Assert.StartsWith("script-", Path.GetFileName(path));
        Assert.EndsWith(".stc", path);
        Assert.Equal(_workspace.DirectoryFor(UserId), Path.GetDirectoryName(path));
        Assert.False(File.Exists(path));
    }

    /// <summary>Even a CLI blow-up takes the temp script with it.</summary>
    [Fact]
    public async Task Should_Delete_The_Temp_Script_When_The_Cli_Throws()
    {
        await Adopt();
        _cli.ScriptFailure = new InvalidOperationException("boom");

        await Assert.ThrowsAsync<InvalidOperationException>(() => _service.RunAsync(UserId, "@filter bw\n"));

        Assert.False(File.Exists(_cli.LastScriptCall!.Value.Script));
    }

    /// <summary>
    /// % lengths resolve against the frame the user is looking at — which is the frame
    /// PlanFrameMapper maps the resulting coordinates back from — so the CLI probes a fresh render.
    /// </summary>
    [Fact]
    public async Task Should_Probe_The_Rendered_Frame_Not_The_Original()
    {
        await Adopt();
        await _editing.SetCropAsync(UserId, "x1=10% x2=50%", album: false);

        await _service.RunAsync(UserId, "@filter bw\n");

        string? input = _cli.LastScriptCall!.Value.Input;
        UserSession session = await _store.GetAsync(UserId);
        Assert.NotNull(input);
        Assert.NotEqual(Path.GetFileName(session.OriginalImagePath), input);
    }

    /// <summary>
    /// The CLI only header-probes the frame, so an edit state that leaves the size alone is probed
    /// on the base image itself — no render is spawned for a size the session already knows.
    /// </summary>
    [Fact]
    public async Task Should_Probe_The_Base_Image_Without_Rendering_When_The_Frame_Keeps_Its_Size()
    {
        await Adopt();
        await _editing.SetFilterAsync(UserId, "bw");
        int before = _cli.EditCalls;

        await _service.RunAsync(UserId, "@filter bw\n");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(session.OriginalImagePath, _cli.LastScriptCall!.Value.Input);
        Assert.Equal(before, _cli.EditCalls);
    }

    [Fact]
    public async Task Should_Probe_Nothing_Without_A_Working_Image()
    {
        _cli.CannedScriptPlan = Plan(ScriptBlock.KIND_URL, "http://203.0.113.9/a.png",
            """[{"op":"openUrl","url":"http://203.0.113.9/a.png"}]""");

        await _service.RunAsync(UserId, "@source http://203.0.113.9/a.png:\n  @save\n");

        Assert.Null(_cli.LastScriptCall!.Value.Input);
    }

    /// <summary>Any error diagnostic means nothing ran — the reply names line, column and code.</summary>
    [Fact]
    public async Task Should_Run_Nothing_When_The_Script_Has_An_Error()
    {
        await Adopt();
        _cli.CannedScriptPlan = new ScriptPlan(
            [new ScriptDiagnostic("error", "E_UNKNOWN_DIRECTIVE", 2, 3, "unknown directive '@crp'")], []);

        ScriptOutcome outcome = await _service.RunAsync(UserId, "@crp 10%\n");

        Assert.Contains("Line 2:3 — unknown directive '@crp' [E_UNKNOWN_DIRECTIVE]", outcome.Reply);
        Assert.False(outcome.Mutated);
        Assert.Null((await _store.GetAsync(UserId)).Edits.Filter);
    }

    [Fact]
    public async Task Should_Report_A_Warning_Without_Blocking_The_Run()
    {
        await Adopt();
        _cli.CannedScriptPlan = new ScriptPlan(
            [new ScriptDiagnostic("warning", "W_NO_SAVE", 1, 1, "the block never saves")],
            [new ScriptBlock(0, "", ScriptBlock.KIND_PROJECT, ["""[{"op":"filter","mode":"bw"}]"""])]);

        ScriptOutcome outcome = await _service.RunAsync(UserId, "@filter bw\n");

        Assert.Contains("Line 1:1 — the block never saves [W_NO_SAVE]", outcome.Warnings);
        Assert.Equal("bw", (await _store.GetAsync(UserId)).Edits.Filter);
    }

    [Theory]
    [InlineData("")]
    [InlineData("   \n\t")]
    public async Task Should_Refuse_An_Empty_Script_Without_Spawning(string text)
    {
        ScriptOutcome outcome = await _service.RunAsync(UserId, text);

        Assert.Contains("empty", outcome.Reply);
        Assert.Null(_cli.LastScriptCall);
    }

    [Fact]
    public async Task Should_Refuse_A_Script_Past_The_Character_Cap()
    {
        ScriptOutcome outcome = await _service.RunAsync(UserId, new string('#', ScriptService.MAX_SCRIPT_CHARS + 1));

        Assert.Contains("too long", outcome.Reply);
        Assert.Null(_cli.LastScriptCall);
    }

    /// <summary>
    /// Verbatim stdout from `stencil -i sample.png --script-plan`: the envelope the bot actually
    /// receives, parsed and executed, so an upstream shape change fails here rather than in a chat.
    /// </summary>
    [Fact]
    public async Task Should_Run_A_Real_Cli_Envelope()
    {
        await Adopt();
        _cli.CannedScriptPlan = CliOutcomeParser.ParseScriptPlan(
            """
            {"version":1,"script":"s.stc","diagnostics":[],"blocks":[{"index":0,"source":"","sourceKind":"project","inputs":["sample.png"],"frame":0,"dims":{"width":16,"height":12},"plans":[{"reply":"","actions":[{"op":"crop","spec":{"x1":"10%","x2":"-10%","y1":"10%","y2":"-10%"}},{"op":"filter","mode":"bw"},{"op":"layout","lines":[{"points":[{"x":0,"y":0},{"x":12.8,"y":9.600000000000001}],"color":"#FFFF00","style":"solid","fillColor":"transparent","thickness":2,"pointSize":4,"locked":false}]}]}],"saves":[]}]}
            """);

        ScriptOutcome outcome = await _service.RunAsync(UserId, "@crop 10%\n@filter bw\n@line (0,0) (100%,100%)\n");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("Script ran: 3 ops.", outcome.Reply);
        Assert.Equal("x1=10% x2=-10% y1=10% y2=-10%", session.Edits.CropSpec);
        Assert.Equal("bw", session.Edits.Filter);
        Assert.Equal(1, session.Edits.LineCount);
    }

    /// <summary>The registry's MAX_ACTIONS is what the CLI chunks at; a chunk past it is refused.</summary>
    [Fact]
    public async Task Should_Refuse_A_Chunk_Over_The_Envelopes_Action_Cap()
    {
        await Adopt();
        string actions = "[" + string.Join(",", Enumerable.Repeat("""{"op":"filter","mode":"bw"}""", 17)) + "]";
        _cli.CannedScriptPlan = Plan(ScriptBlock.KIND_PROJECT, "", actions);

        ScriptOutcome outcome = await _service.RunAsync(UserId, "@filter bw\n");

        Assert.Contains("can't run", outcome.Reply);
    }
}
