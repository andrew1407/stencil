using System.Text;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Tests;

/// <summary>A <c>.stc</c> upload is the file form of <c>/script</c>: it lands beside <c>.stencil</c> and <c>.json</c> in <see cref="DocumentIntake"/> and runs the moment it arrives.</summary>
public sealed class ScriptDocumentTests : IDisposable
{
    private const long _userId = 55;
    private const long _chatId = 66;

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly ScriptBotClient _bot = new();
    private readonly InMemorySessionStore _store = new();
    private EditingService _editing = default!;

    public ScriptDocumentTests() =>
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-stc-" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private UpdateRouter makeRouter()
    {
        BotOptions options = new() { DataDir = _dataDir, AllowedUsers = new HashSet<long> { _userId } };
        _editing = new EditingService(_cli, new UserWorkspace(options), _store);
        CommandHandlers handlers = TestHandlers.Create(options, _store, _cli, _bot, editing: _editing);
        return new UpdateRouter(
            handlers, new CallbackAction(handlers, _bot, _store), _editing, _store, _bot,
            new UserGate(), options, new MockLogger<UpdateRouter>());
    }

    private void canned() =>
        _cli.CannedScriptPlan = new ScriptPlan([], [
            new ScriptBlock(0, "", ScriptBlock.KIND_PROJECT, ["""[{"op":"filter","mode":"bw"}]"""]),
        ]);

    private static Message documentFrom(string fileName) =>
        new()
        {
            Id = 1,
            Chat = new Chat { Id = _chatId },
            From = new User { Id = _userId },
            Document = new Document { FileId = "d1", FileUniqueId = "d1", FileName = fileName },
        };

    private string lastText() => _bot.Requests.OfType<SendMessageRequest>().Last().Text;

    [Fact]
    public async Task Should_Run_An_Uploaded_Stc_As_A_Script()
    {
        _bot.Payload = Encoding.UTF8.GetBytes("@filter bw\n");
        UpdateRouter router = makeRouter();
        await _editing.BlankAsync(_userId, new BlankSpec());
        canned();

        await router.HandleMessageAsync(documentFrom("shots.stc"), CancellationToken.None);

        Assert.Equal("@filter bw\n", Assert.Single(_cli.ScriptTexts));
        Assert.Equal("bw", (await _store.GetAsync(_userId)).Edits.Filter);
    }

    /// <summary>A .STC is the same file; the extension match is case-insensitive like the others.</summary>
    [Fact]
    public async Task Should_Match_The_Extension_Whatever_Its_Case()
    {
        _bot.Payload = Encoding.UTF8.GetBytes("@filter bw\n");
        UpdateRouter router = makeRouter();
        await _editing.BlankAsync(_userId, new BlankSpec());
        canned();

        await router.HandleMessageAsync(documentFrom("SHOTS.STC"), CancellationToken.None);

        Assert.Single(_cli.ScriptTexts);
    }

    /// <summary>An editor's BOM would otherwise reach the lexer as a stray token on line 1.</summary>
    [Fact]
    public async Task Should_Strip_A_Utf8_Byte_Order_Mark()
    {
        _bot.Payload = [.. Encoding.UTF8.GetPreamble(), .. Encoding.UTF8.GetBytes("@filter bw\n")];
        UpdateRouter router = makeRouter();
        await _editing.BlankAsync(_userId, new BlankSpec());
        canned();

        await router.HandleMessageAsync(documentFrom("shots.stc"), CancellationToken.None);

        Assert.Equal("@filter bw\n", Assert.Single(_cli.ScriptTexts));
    }

    /// <summary>The same "send a photo first" gate the typed command has.</summary>
    [Fact]
    public async Task Should_Ask_For_A_Photo_When_The_Uploaded_Script_Brings_No_Source()
    {
        _bot.Payload = Encoding.UTF8.GetBytes("@filter bw\n");
        UpdateRouter router = makeRouter();

        await router.HandleMessageAsync(documentFrom("shots.stc"), CancellationToken.None);

        Assert.Contains("send a photo first", lastText());
        Assert.Empty(_cli.ScriptTexts);
    }

    /// <summary>The unsupported-file sentence names every kind the bot does take.</summary>
    [Fact]
    public async Task Should_Name_The_Stc_Among_The_Accepted_Kinds()
    {
        UpdateRouter router = makeRouter();

        await router.HandleMessageAsync(documentFrom("notes.txt"), CancellationToken.None);

        Assert.Contains(".stc script", lastText());
        Assert.Contains(".stencil project", lastText());
        Assert.Contains(".json layout", lastText());
    }

    private sealed class ScriptBotClient : MockBotClient
    {
        public byte[] Payload { get; set; } = [];

        public override Task DownloadFile(string filePath, Stream destination, CancellationToken cancellationToken = default) =>
            destination.WriteAsync(Payload, cancellationToken).AsTask();
    }
}
