using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Stencil.TelegramBot.Bot.Telegram.Commands;
using Stencil.TelegramBot.Bot.Telegram.Messaging;

namespace Stencil.TelegramBot.Tests;

/// <summary><c>/script</c>: the usage line, the "send a photo first" gate, and the result going out through the SAME render-and-send path every slash command uses.</summary>
public sealed class ScriptCommandTests : IDisposable
{
    private const long _userId = 55;
    private const long _chatId = 66;

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly MockBotClient _bot = new();
    private readonly InMemorySessionStore _store = new();
    private readonly EditingService _editing;
    private readonly CommandHandlers _handlers;

    public ScriptCommandTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-script-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir };
        _editing = new EditingService(_cli, new UserWorkspace(options), _store);
        _handlers = TestHandlers.Create(options, _store, _cli, _bot, editing: _editing);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private Task dispatch(string text) =>
        _handlers.DispatchAsync(_userId, _chatId, CommandParser.Parse(text), CancellationToken.None);

    private string lastText() => _bot.Requests.OfType<SendMessageRequest>().Last().Text;

    private void canned(params string[] actionArrays) =>
        _cli.CannedScriptPlan = new ScriptPlan(
            [], [new ScriptBlock(0, "", ScriptBlock.KIND_PROJECT, actionArrays)]);

    [Fact]
    public async Task Should_Answer_A_Bare_Script_With_The_Usage_Line()
    {
        await dispatch("/script");

        Assert.Contains("Usage: /script", lastText());
        Assert.Null(_cli.LastScriptCall);
    }

    [Fact]
    public async Task Should_Ask_For_A_Photo_When_The_Script_Brings_No_Source()
    {
        await dispatch("/script @filter bw");

        Assert.Contains("send a photo first", lastText());
        Assert.Null(_cli.LastScriptCall);
    }

    /// <summary>A script that opens its own image needs no working one.</summary>
    [Fact]
    public async Task Should_Run_A_Sourced_Script_Without_A_Working_Image()
    {
        _cli.CannedScriptPlan = new ScriptPlan([], [
            new ScriptBlock(0, "http://203.0.113.9/a.png", ScriptBlock.KIND_URL,
                ["""[{"op":"openUrl","url":"http://203.0.113.9/a.png"}]"""]),
        ]);

        await dispatch("/script @source http://203.0.113.9/a.png:\n  @save");

        Assert.NotNull(_cli.LastScriptCall);
        Assert.True((await _store.GetAsync(_userId)).HasImage);
    }

    [Fact]
    public async Task Should_Send_The_Result_Through_The_Usual_Render_Path()
    {
        await _editing.BlankAsync(_userId, new BlankSpec());
        canned("""[{"op":"filter","mode":"sepia"}]""");

        await dispatch("/script @filter sepia");

        Assert.Single(_bot.Requests.OfType<SendPhotoRequest>());
        Assert.Equal("sepia", (await _store.GetAsync(_userId)).Edits.Filter);
    }

    /// <summary>Multi-line scripts survive the command parser, newlines and all.</summary>
    [Fact]
    public async Task Should_Keep_The_Whole_Multi_Line_Script()
    {
        await _editing.BlankAsync(_userId, new BlankSpec());
        canned("""[{"op":"filter","mode":"bw"}]""");

        await dispatch("/script @crop 10%\n@filter bw");

        Assert.Equal("@crop 10%\n@filter bw", Assert.Single(_cli.ScriptTexts));
    }

    [Fact]
    public async Task Should_Report_A_Script_Error_Without_Sending_A_Picture()
    {
        await _editing.BlankAsync(_userId, new BlankSpec());
        _cli.CannedScriptPlan = new ScriptPlan(
            [new ScriptDiagnostic("error", "E_MISSING_COLON", 1, 8, "a block header needs ':'")], []);

        await dispatch("/script @source a.png");

        Assert.Contains("Line 1:8 — a block header needs ':' [E_MISSING_COLON]", lastText());
        Assert.Empty(_bot.Requests.OfType<SendPhotoRequest>());
    }

    /// <summary>The progress notice goes up and is deleted again, like the /prompt one.</summary>
    [Fact]
    public async Task Should_Raise_And_Clear_A_Progress_Notice()
    {
        await _editing.BlankAsync(_userId, new BlankSpec());
        canned("""[{"op":"filter","mode":"bw"}]""");

        await dispatch("/script @filter bw");

        SendMessageRequest notice = _bot.Requests.OfType<SendMessageRequest>().First();
        Assert.Equal(ProgressNotice.Frame(0, Replies.ScriptWorking()), notice.Text);
        Assert.Single(_bot.Requests.OfType<DeleteMessageRequest>());
    }

    /// <summary>A script's ops are ordinary edits, so /undo walks them back one at a time.</summary>
    [Fact]
    public async Task Should_Leave_A_Scripts_Edits_On_The_Normal_Undo_Stack()
    {
        await _editing.BlankAsync(_userId, new BlankSpec());
        canned("""[{"op":"filter","mode":"bw"}]""");
        await dispatch("/script @filter bw");

        await dispatch("/undo");

        UserSession session = await _store.GetAsync(_userId);
        Assert.Null(session.Edits.Filter);
    }
}
