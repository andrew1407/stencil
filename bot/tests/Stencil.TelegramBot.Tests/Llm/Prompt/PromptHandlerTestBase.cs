using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Types;
using Stencil.TelegramBot.Bot.Telegram.Commands;
using Stencil.TelegramBot.Bot.Telegram.Messaging;
using Stencil.TelegramBot.Tests.Telegram;

namespace Stencil.TelegramBot.Tests.Llm.Prompt;

/// <summary>The shared rig for the <c>/prompt</c> suites: the real <see cref="CommandHandlers"/> + <see cref="PromptService"/> + <see cref="EditingService"/> with the LLM, CLI and Telegram mocked, plus a tap that rides the same callback path the buttons do.</summary>
public abstract class PromptHandlerTestBase : IDisposable
{
    protected const long UserId = 55;
    protected const long ChatId = 66;

    protected readonly string _dataDir;
    protected readonly MockStencilCli _cli = new();
    protected readonly MockBotClient _bot = new();
    protected readonly MockLlmClient _llm = new();
    protected readonly InMemorySessionStore _store = new();
    protected readonly CommandHandlers _handlers;
    protected readonly CallbackAction _callbacks;

    protected PromptHandlerTestBase()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-promptcmd-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir };
        _handlers = TestHandlers.Create(options, _store, _cli, _bot, _llm);
        _callbacks = new CallbackAction(_handlers, _bot, _store);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
        GC.SuppressFinalize(this);
    }

    protected Task Dispatch(string text) =>
        _handlers.DispatchAsync(UserId, ChatId, CommandParser.Parse(text), CancellationToken.None);

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
}
