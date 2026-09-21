using Microsoft.Extensions.Logging.Abstractions;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types;
using Telegram.Bot.Types.ReplyMarkups;
using Stencil.TelegramBot.Bot.Telegram.Access;
using Stencil.TelegramBot.Bot.Telegram.Commands;
using Stencil.TelegramBot.Bot.Telegram.Messaging;
using Stencil.TelegramBot.Domain.Llm.Wire;

namespace Stencil.TelegramBot.Tests;

/// <summary>The ⏹ Stop button through the real <see cref="UpdateRouter"/>, so the gate bypass is exercised: a running turn holds the user's <see cref="UserGate"/>, so a stop tap routed the normal way would queue behind the very turn it cancels.</summary>
public sealed class PromptStopTests : IDisposable
{
    private const long _userId = 77;
    private const long _chatId = 88;

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly MockBotClient _bot = new();
    private readonly MockLlmClient _llm = new();
    private readonly InMemorySessionStore _store = new();
    private readonly PromptCancellations _cancellations = new();
    private readonly CommandHandlers _handlers;
    private readonly UpdateRouter _router;

    public PromptStopTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-stop-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir, AllowedUsers = AnyUser.Instance };
        EditingService editing = new(_cli, new UserWorkspace(options), _store);
        _handlers = TestHandlers.Create(options, _store, _cli, _bot, _llm, editing: editing, cancellations: _cancellations);
        CallbackAction callbacks = new(_handlers, _bot, _store, _cancellations);
        _router = new UpdateRouter(
            _handlers, callbacks, editing, _store, _bot, new UserGate(), options,
            NullLogger<UpdateRouter>.Instance);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private Task dispatch(string text) =>
        _handlers.DispatchAsync(_userId, _chatId, CommandParser.Parse(text), CancellationToken.None);

    /// <summary>Tap a button the way the poller does — through the router, gate and all.</summary>
    private Task tap(string data) =>
        _router.HandleUpdateAsync(
            new Update
            {
                CallbackQuery = new CallbackQuery
                {
                    Id = "cb",
                    From = new User { Id = _userId },
                    Message = new Message { Chat = new Chat { Id = _chatId } },
                    Data = data,
                },
            },
            CancellationToken.None);

    private IEnumerable<SendMessageRequest> Messages => _bot.Requests.OfType<SendMessageRequest>();

    [Fact]
    public async Task Should_Carry_A_Stop_Button_On_The_Working_Notice()
    {
        await dispatch("/blank");
        _llm.CannedReplies.Enqueue(new LlmReply("""{"reply":"ok","actions":[]}"""));

        await dispatch("/prompt anything");

        SendMessageRequest notice = Messages.First(m => m.Text.Contains("Working on your request"));
        InlineKeyboardMarkup keyboard = Assert.IsType<InlineKeyboardMarkup>(notice.ReplyMarkup);
        Assert.Equal(CallbackAction.STOP_TOKEN, keyboard.InlineKeyboard.Single().Single().CallbackData);
    }

    [Fact]
    public async Task Should_End_The_Running_Turn_As_Plain_Info_On_Stop()
    {
        await dispatch("/blank");
        _llm.BlockUntilCancelled = true;
        Task turn = dispatch("/prompt take your time");
        await _llm.InFlight.Task.WaitAsync(TimeSpan.FromSeconds(10));

        // The tap lands WHILE the turn holds the user's gate — it must not block on it.
        await tap(CallbackAction.STOP_TOKEN).WaitAsync(TimeSpan.FromSeconds(10));
        await turn.WaitAsync(TimeSpan.FromSeconds(10));

        Assert.Contains(Messages, m => m.Text.Contains("Stopping"));
        SendMessageRequest last = Messages.Last();
        Assert.Contains("Stopped", last.Text);
        // A stop is what the user asked for: info, no error glyph…
        Assert.DoesNotContain("🔴", last.Text);
        // …and the line names the button under it — a bubble is laid out at its keyboard's
        // width, so a bare "Stopped." would sit in a wide empty box.
        Assert.Contains("Retry", last.Text);
        // …but the request went unanswered, so it carries the same Retry button a failure does,
        // and the prompt is parked for that button to re-run.
        InlineKeyboardMarkup retry = Assert.IsType<InlineKeyboardMarkup>(last.ReplyMarkup);
        Assert.Equal("retry:prompt", retry.InlineKeyboard.Single().Single().CallbackData);
        Assert.Equal("take your time", (await _store.GetAsync(_userId)).LastRetryablePrompt);
        // The turn was cancelled before it could plan anything, so the image is untouched.
        Assert.Single(_bot.Requests.OfType<SendPhotoRequest>());
    }

    [Fact]
    public async Task Should_Re_Run_The_Same_Prompt_On_Retry_After_A_Stop()
    {
        await dispatch("/blank");
        _llm.BlockUntilCancelled = true;
        Task turn = dispatch("/prompt take your time");
        await _llm.InFlight.Task.WaitAsync(TimeSpan.FromSeconds(10));
        await tap(CallbackAction.STOP_TOKEN).WaitAsync(TimeSpan.FromSeconds(10));
        await turn.WaitAsync(TimeSpan.FromSeconds(10));

        // Second time round the model answers, so the retried turn lands like any other.
        _llm.BlockUntilCancelled = false;
        _llm.CannedReplies.Enqueue(new LlmReply("""{"reply":"done now","actions":[]}"""));
        await tap("retry:prompt").WaitAsync(TimeSpan.FromSeconds(10));

        Assert.Equal(2, _llm.Requests.Count);
        Assert.Contains("take your time", _llm.Requests[^1].Messages[^1].Text);
        Assert.Contains(Messages, m => m.Text.Contains("done now"));
        // The turn landed, so the button has nothing left to re-run.
        Assert.Null((await _store.GetAsync(_userId)).LastRetryablePrompt);
    }

    [Fact]
    public async Task Should_Say_So_On_Stop_With_Nothing_Running()
    {
        await dispatch("/blank");

        await tap(CallbackAction.STOP_TOKEN);

        Assert.Contains("already finished", Messages.Last().Text);
    }
}
