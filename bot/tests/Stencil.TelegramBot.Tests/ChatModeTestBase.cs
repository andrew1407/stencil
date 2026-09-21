using Microsoft.Extensions.Logging.Abstractions;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types;
using Stencil.TelegramBot.Bot.Telegram.Commands;
using Stencil.TelegramBot.Bot.Telegram.Messaging;
using Stencil.TelegramBot.Bot.Telegram.Access;

namespace Stencil.TelegramBot.Tests;

/// <summary>The shared rig for the chat-mode suites: the real <see cref="UpdateRouter"/> + <see cref="CommandHandlers"/> + <see cref="PromptService"/> over mocked LLM, CLI and Telegram.</summary>
public abstract class ChatModeTestBase : IDisposable
{
    protected const long UserId = 91;
    protected const long ChatId = 92;

    protected readonly string _dataDir;
    protected readonly MockStencilCli _cli = new();
    protected readonly MockBotClient _bot = new();
    protected readonly MockLlmClient _llm = new();
    protected readonly InMemorySessionStore _store = new();
    protected readonly CommandHandlers _handlers;
    protected readonly CallbackAction _callbacks;
    protected readonly UpdateRouter _router;

    protected ChatModeTestBase()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-chatmode-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir, AllowedUsers = AnyUser.Instance };
        EditingService editing = new(_cli, new UserWorkspace(options), _store);
        _handlers = TestHandlers.Create(options, _store, _cli, _bot, _llm, editing: editing);
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

    /// <summary>Route a plain Telegram text message (command or not) exactly as the poller would.</summary>
    protected Task Send(string text) =>
        _router.HandleMessageAsync(
            new Message
            {
                Chat = new Chat { Id = ChatId },
                From = new User { Id = UserId },
                Text = text,
            },
            CancellationToken.None);

    /// <summary>Tap an inline button (the 💬 Chat / 🚪 Chat off tokens).</summary>
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
}
