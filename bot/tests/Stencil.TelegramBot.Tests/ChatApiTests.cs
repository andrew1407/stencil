using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Tests;

/// <summary>The <c>/chatapi</c> picker: listing the operator's configured chat APIs, selecting one per user (by command or button), and that the selection is what the next turn calls.</summary>
public sealed class ChatApiTests : IDisposable
{
    private const long _userId = 91;
    private const long _chatId = 92;

    private static readonly LlmProfile _local = new()
    {
        Name = "llama",
        Label = "Llama (local)",
        Options = new LlmOptions
        {
            Provider = LlmOptions.PROVIDER_OLLAMA,
            BaseUrl = "http://192.168.1.9:11434",
            Model = "llama3.2-vision",
        },
    };

    private static readonly LlmProfile _studio = new()
    {
        Name = "lmstudio",
        Label = "LM Studio",
        Options = new LlmOptions
        {
            Provider = LlmOptions.PROVIDER_OPEN_AI_COMPAT,
            BaseUrl = "http://localhost:1234/v1",
        },
    };

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly MockBotClient _bot = new();
    private readonly MockLlmClient _llm = new();
    private readonly InMemorySessionStore _store = new();
    private readonly CommandHandlers _handlers;
    private readonly CallbackAction _callbacks;

    public ChatApiTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-chatapi-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir, LlmProfiles = [_local, _studio] };
        _handlers = TestHandlers.Create(options, _store, _cli, _bot, _llm, profiles: [_local, _studio]);
        _callbacks = new CallbackAction(_handlers, _bot, _store);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private Task dispatch(string text) =>
        _handlers.DispatchAsync(_userId, _chatId, CommandParser.Parse(text), CancellationToken.None);

    private Task tap(string data) =>
        _callbacks.HandleAsync(
            new CallbackQuery
            {
                Id = "cb",
                From = new User { Id = _userId },
                Message = new Message { Chat = new Chat { Id = _chatId } },
                Data = data,
            },
            CancellationToken.None);

    private IEnumerable<SendMessageRequest> Messages => _bot.Requests.OfType<SendMessageRequest>();

    [Fact]
    public async Task Should_List_Every_Configured_Api_With_A_Button_Each_On_Bare_Chat_Api()
    {
        await dispatch("/chatapi");

        SendMessageRequest list = Assert.Single(Messages);
        Assert.Contains("Llama (local)", list.Text);
        Assert.Contains("llama3.2-vision", list.Text);
        Assert.Contains("LM Studio", list.Text);
        // Nothing picked yet, so the bot's own configuration is what's in use.
        Assert.Contains("the bot's default", list.Text);
        InlineKeyboardMarkup keyboard = Assert.IsType<InlineKeyboardMarkup>(list.ReplyMarkup);
        Assert.Equal(
            new[] { "api:llama", "api:lmstudio" },
            keyboard.InlineKeyboard.Select(r => r.Single().CallbackData));
    }

    [Fact]
    public async Task Should_Store_The_Pick_Per_User_And_Call_It_On_The_Next_Turn()
    {
        await tap("api:llama");

        Assert.Equal("llama", (await _store.GetAsync(_userId)).LlmProfile);
        Assert.Contains("Llama (local)", Messages.Last().Text);

        await dispatch("/blank");
        await dispatch("/prompt make it sepia");

        // The turn ran against the picked profile, not the bot's default.
        LlmOptions used = Assert.Single(_llm.Requests).Options!;
        Assert.Equal(LlmOptions.PROVIDER_OLLAMA, used.Provider);
        Assert.Equal("http://192.168.1.9:11434", used.BaseUrl);
        Assert.Equal("llama3.2-vision", used.Model);
    }

    [Fact]
    public async Task Should_Tick_The_Current_Api_And_Select_By_Name()
    {
        await dispatch("/chatapi lmstudio");
        Assert.Equal("lmstudio", (await _store.GetAsync(_userId)).LlmProfile);

        await dispatch("/chatapi");

        SendMessageRequest list = Messages.Last();
        Assert.Contains("Now using: LM Studio", list.Text);
        InlineKeyboardMarkup keyboard = Assert.IsType<InlineKeyboardMarkup>(list.ReplyMarkup);
        Assert.Contains("✅", keyboard.InlineKeyboard.Last().Single().Text);
    }

    [Fact]
    public async Task Should_Name_The_Existing_Apis_For_An_Unknown_Name()
    {
        await dispatch("/chatapi gpt5");

        SendMessageRequest reply = Assert.Single(Messages);
        Assert.Contains("No chat API called", reply.Text);
        Assert.Contains("llama", reply.Text);
        Assert.Contains("lmstudio", reply.Text);
        Assert.Null((await _store.GetAsync(_userId)).LlmProfile);
    }

    [Fact]
    public async Task Should_Fall_Back_To_The_Default_Instead_Of_Failing_For_A_Profile_The_Operator_Removed()
    {
        // A session left pointing at a profile that is no longer configured (an env change
        // between restarts) must still get a working turn.
        await _store.SaveAsync((await _store.GetAsync(_userId)) with { LlmProfile = "gone" });

        await dispatch("/blank");
        await dispatch("/prompt make it sepia");

        LlmOptions used = Assert.Single(_llm.Requests).Options!;
        Assert.Equal(new LlmOptions().Provider, used.Provider);
    }
}
