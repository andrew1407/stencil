using Microsoft.Extensions.Logging.Abstractions;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The reply-tone convention (<see cref="Replies.Tag"/>) as the user meets it: one
/// representative reply per category, driven through the real handlers. The glyph mapping
/// itself needs no rig and lives in <see cref="RepliesTests"/>.
/// </summary>
public sealed class ReplyTonesTests : IDisposable
{
    private const long _userId = 71;
    private const long _chatId = 72;

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly MockBotClient _bot = new();
    private readonly MockLlmClient _llm = new();
    private readonly InMemorySessionStore _store = new();
    private readonly RecordingServerService _servers = new();
    private readonly CommandHandlers _handlers;
    private readonly UpdateRouter _router;

    public ReplyTonesTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-tones-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir, AllowedUsers = AnyUser.Instance };
        EditingService editing = new(_cli, new UserWorkspace(options), _store);
        _handlers = TestHandlers.Create(options, _store, _cli, _bot, _llm, servers: _servers, editing: editing);
        _router = new UpdateRouter(
            _handlers,
            new CallbackAction(_handlers, _bot, _store),
            editing,
            _store,
            _bot,
            new UserGate(),
            options,
            NullLogger<UpdateRouter>.Instance);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    /// <summary>Route a plain Telegram text message exactly as the poller would.</summary>
    private Task send(string text) =>
        _router.HandleMessageAsync(
            new Message { Chat = new Chat { Id = _chatId }, From = new User { Id = _userId }, Text = text },
            CancellationToken.None);

    private Task dispatch(string text) =>
        _handlers.DispatchAsync(_userId, _chatId, CommandParser.Parse(text), CancellationToken.None);

    private string lastText() => _bot.Requests.OfType<SendMessageRequest>().Last().Text;

    // The reported case: /connect against a server that rejects the token.
    [Fact]
    public async Task AConnectionFailureIsMarkedAsAnError()
    {
        _servers.ConnectThrows = new ServerException("unauthorized", "missing or invalid token", 401);

        await send("/connect https://stencil.example.com bad-token");

        Assert.StartsWith("🔴 ", lastText());
        // The wording itself is untouched — only the prefix is new.
        Assert.Contains("missing or invalid token", lastText());
    }

    [Fact]
    public async Task ACliFailureIsMarkedAsAnErrorToo()
    {
        _cli.FailWhen = _ => true;

        await send("/blank");

        Assert.Equal("🔴 canned CLI failure", lastText());
    }

    [Fact]
    public async Task AnAssistantFailureIsMarkedAsAnError()
    {
        await dispatch("/blank");
        _llm.Throw = new LlmException("Could not reach the AI service.");

        await dispatch("/prompt make it sepia");

        Assert.Equal("🔴 Could not reach the AI service.", lastText());
    }

    // A plan that half-ran: the reply stays plain, each warning line carries the warning glyph.
    [Fact]
    public async Task PlanWarningsAreMarkedAsWarnings()
    {
        await dispatch("/blank");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"reply":"Rotated it.","actions":[{"op":"rotate","dir":"right"},{"op":"save"}]}"""));

        await dispatch("/prompt rotate and save it");

        string text = _bot.Requests.OfType<SendMessageRequest>().Last().Text;
        Assert.StartsWith("Rotated it.", text);
        Assert.Contains("🟡 ", text);
        Assert.DoesNotContain("🟡 🟡", text);
    }

    [Fact]
    public async Task AConfirmedServerActionIsMarkedAsSuccess()
    {
        await send("/connect https://stencil.example.com");

        Assert.Equal("✅ Connected to https://stencil.example.com.", lastText());
    }

    [Fact]
    public async Task APlainNoticeIsMarkedAsANotice()
    {
        // Nothing failed and nothing changed: /disconnect with no matching connection.
        _servers.DisconnectResult = false;

        await dispatch("/disconnect https://nowhere.example.com");

        Assert.Equal("ℹ️ No matching connection to disconnect.", lastText());
    }
}
