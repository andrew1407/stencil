using Microsoft.Extensions.Logging.Abstractions;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types;
using Stencil.TelegramBot.Bot.Telegram.Commands;
using Stencil.TelegramBot.Bot.Telegram.Messaging;
using Stencil.TelegramBot.Bot.Telegram.Access;
using Stencil.TelegramBot.Tests.Telegram;

namespace Stencil.TelegramBot.Tests.Llm.Conversation;

/// <summary>The shared rig for the §12 chat-persistence suites: the real router, handlers, prompt and server services over in-memory mocks, plus a seeded-and-opened server project.</summary>
public abstract class ChatPersistenceTestBase : IDisposable
{
    protected const long UserId = 95;
    protected const long ChatId = 96;
    protected const string Server = "http://srv:8090";
    protected const string ProjectId = "p_1";

    protected readonly string _dataDir;
    protected readonly MockStencilCli _cli = new();
    protected readonly MockBotClient _bot = new();
    protected readonly MockLlmClient _llm = new();
    protected readonly InMemorySessionStore _store = new();
    protected readonly MockServerClientFactory _factory = new();
    protected readonly CommandHandlers _handlers;
    protected readonly CallbackAction _callbacks;
    protected readonly UpdateRouter _router;

    protected ChatPersistenceTestBase()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-chatsave-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir, AllowedUsers = AnyUser.Instance };
        EditingService editing = new(_cli, new UserWorkspace(options), _store);
        ServerService servers = new(_factory, _store, editing);
        _handlers = TestHandlers.Create(options, _store, _cli, _bot, _llm, servers: servers, editing: editing);
        _callbacks = new CallbackAction(_handlers, _bot, _store);
        _router = new UpdateRouter(
            _handlers,
            _callbacks,
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
        GC.SuppressFinalize(this);
    }

    protected MockStencilServerClient ServerClient => _factory.ClientFor(Server);

    protected Task Send(string text) =>
        _router.HandleMessageAsync(
            new Message
            {
                Chat = new Chat { Id = ChatId },
                From = new User { Id = UserId },
                Text = text,
            },
            CancellationToken.None);

    protected Task Tap(string data) =>
        _callbacks.HandleAsync(
            new CallbackQuery
            {
                Id = "cb",
                From = new User { Id = UserId },
                Message = new Message { Chat = new Chat { Id = ChatId } },
                Data = data,
            },
            CancellationToken.None);

    protected IEnumerable<SendMessageRequest> Messages => _bot.Requests.OfType<SendMessageRequest>();

    /// <summary>Seed a server project and open it, bypassing DNS-touching /connect validation.</summary>
    protected async Task OpenProjectAsync()
    {
        ServerClient.Seed(new ProjectRecord
        {
            Id = ProjectId,
            Name = "Poster",
            HasImage = true,
            ImageW = 320,
            ImageH = 240,
            Version = 3,
        });
        UserSession session = await _store.GetAsync(UserId);
        await _store.SaveAsync(session with
        {
            Connections = [new ServerConnectionInfo { Url = Server, Token = "tok", VerifyTls = true }],
        });
        await Send("/fetch Poster");
    }

    protected IEnumerable<(string Id, string Kind, byte[] Data, string Ext, int W, int H)> ChatPuts =>
        ServerClient.Puts.Where(p => p.Kind == ProjectFileKind.CHAT);

}
