using Microsoft.Extensions.Logging.Abstractions;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Chat mode (<c>/chat</c> + the 💬 buttons) through the real <see cref="UpdateRouter"/> +
/// <see cref="CommandHandlers"/> + <see cref="PromptService"/>, with the LLM, CLI and Telegram
/// mocked: toggling the session flag on/off, plain text routed to the same prompt path (variants
/// included), commands never swallowed, and a pending free-text flow winning over chat mode.
/// </summary>
public sealed class ChatModeTests : IDisposable
{
    private const long UserId = 91;
    private const long ChatId = 92;

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly MockBotClient _bot = new();
    private readonly MockLlmClient _llm = new();
    private readonly InMemorySessionStore _store = new();
    private readonly CommandHandlers _handlers;
    private readonly CallbackAction _callbacks;
    private readonly UpdateRouter _router;

    public ChatModeTests()
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
    }

    /// <summary>Route a plain Telegram text message (command or not) exactly as the poller would.</summary>
    private Task Send(string text) =>
        _router.HandleMessageAsync(
            new Message
            {
                Chat = new Chat { Id = ChatId },
                From = new User { Id = UserId },
                Text = text,
            },
            CancellationToken.None);

    /// <summary>Tap an inline button (the 💬 Chat / 🚪 Chat off tokens).</summary>
    private Task Tap(string data) =>
        _callbacks.HandleAsync(
            new CallbackQuery
            {
                Id = "cb",
                From = new User { Id = UserId },
                Message = new Message { Chat = new Chat { Id = ChatId } },
                Data = data,
            },
            CancellationToken.None);

    private IEnumerable<SendMessageRequest> Messages => _bot.Requests.OfType<SendMessageRequest>();

    [Fact]
    public async Task ChatCommandTurnsTheModeOnConfirmsAndOffersTheWayBack()
    {
        await Send("/chat");

        UserSession session = await _store.GetAsync(UserId);
        Assert.True(session.ChatMode);
        SendMessageRequest confirm = Assert.Single(Messages);
        Assert.Contains("Chat mode on", confirm.Text);
        Assert.Contains("/chat off", confirm.Text);
        InlineKeyboardMarkup markup = Assert.IsType<InlineKeyboardMarkup>(confirm.ReplyMarkup);
        Assert.Contains(markup.InlineKeyboard.SelectMany(row => row), b => b.CallbackData == "chat:off");
    }

    [Fact]
    public async Task ChatOffTurnsItBackOffAndConfirms()
    {
        await Send("/chat");
        await Send("/chat off");

        UserSession session = await _store.GetAsync(UserId);
        Assert.False(session.ChatMode);
        Assert.Contains("Chat mode off", Messages.Last().Text);
    }

    [Fact]
    public async Task TheButtonsToggleTheSameFlagAsTheCommand()
    {
        await Tap("chat:on");
        Assert.True((await _store.GetAsync(UserId)).ChatMode);

        await Tap("chat:off");
        Assert.False((await _store.GetAsync(UserId)).ChatMode);
    }

    [Fact]
    public void TheMainMenuAndEditMenuSurfaceTheChatButton()
    {
        Assert.Contains(Keyboards.MainMenu().InlineKeyboard.SelectMany(r => r), b => b.CallbackData == "chat:on");
        Assert.Contains(Keyboards.EditMenu(false).InlineKeyboard.SelectMany(r => r), b => b.CallbackData == "chat:on");
    }

    [Fact]
    public async Task AnUnknownArgumentJustExplainsTheCommandAndChangesNothing()
    {
        await Send("/chat whenever");

        Assert.False((await _store.GetAsync(UserId)).ChatMode);
        Assert.Contains("/chat off to stop", Assert.Single(Messages).Text);
    }

    [Fact]
    public async Task AnImageLinkWithInstructionsLoadsItThenRunsThemThroughTheAssistant()
    {
        await Send("/chat");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"reply":"Made it black & white.","actions":[{"op":"filter","mode":"bw"}]}"""));

        await Send("upload image b&w, highlight face and head: https://example.com/a.jpg");

        // The link still loads as the working image (rendered back to the chat)…
        Assert.NotEmpty(_bot.Requests.OfType<SendPhotoRequest>());
        // …with an explicit "still editing" note before the slow assistant turn (the /url
        // render's edit menu otherwise reads as "done").
        Assert.Contains(Messages, m => m.Text.Contains("now editing"));
        // …and the words around it reached the assistant, WITHOUT the URL in the prompt text.
        LlmChatRequest turn = Assert.Single(_llm.Requests);
        string sent = turn.Messages.Last().Text;
        Assert.Contains("highlight face and head", sent);
        Assert.DoesNotContain("https://example.com/a.jpg", sent);
        Assert.Equal("bw", _cli.LastRequest!.Filter);
    }

    [Fact]
    public async Task ABareImageLinkStillJustLoadsItEvenInChatMode()
    {
        await Send("/chat");

        await Send("https://example.com/a.jpg");

        Assert.NotEmpty(_bot.Requests.OfType<SendPhotoRequest>());
        Assert.Empty(_llm.Requests);   // nothing to ask about — the link is the whole message
    }

    [Fact]
    public async Task OutsideChatModeALinkWithWordsKeepsLoadingTheLinkOnly()
    {
        await Send("look at this https://example.com/a.jpg");

        Assert.NotEmpty(_bot.Requests.OfType<SendPhotoRequest>());
        Assert.Empty(_llm.Requests);
    }

    [Fact]
    public async Task PlainTextInChatModeGoesToTheAssistantAndRendersLikePrompt()
    {
        await Send("/blank");
        await Send("/chat");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"reply":"Made it black & white.","actions":[{"op":"filter","mode":"bw"}]}"""));

        await Send("make it black and white");

        Assert.Single(_llm.Requests);
        Assert.Equal("Made it black & white.", Messages.Last().Text);
        Assert.Equal("bw", _cli.LastRequest!.Filter);
        // /blank rendered one photo; the chat turn's result is the second.
        Assert.Equal(2, _bot.Requests.OfType<SendPhotoRequest>().Count());
        // Chat mode stays on until it is switched off.
        Assert.True((await _store.GetAsync(UserId)).ChatMode);
    }

    [Fact]
    public async Task VariantsStillComeBackAsOneMediaGroupInChatMode()
    {
        await Send("/blank");
        await Send("/chat");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """
            {"reply":"Two takes.","actions":[],"variants":[
              {"label":"rotated","actions":[{"op":"rotate","dir":"left"}]},
              {"label":"sepia","actions":[{"op":"filter","mode":"sepia"}]}]}
            """));

        await Send("give me two variants");

        SendMediaGroupRequest album = Assert.Single(_bot.Requests.OfType<SendMediaGroupRequest>());
        Assert.Equal(2, album.Media.Count());
    }

    [Fact]
    public async Task WithoutChatModePlainTextStillGetsTheHintAndNeverReachesTheLlm()
    {
        await Send("make it black and white");

        Assert.Empty(_llm.Requests);
        Assert.Contains("Send a photo or an image link", Assert.Single(Messages).Text);
    }

    [Fact]
    public async Task CommandsAreNeverSwallowedByChatMode()
    {
        await Send("/chat");

        await Send("/blank");

        // The command rendered a blank canvas instead of being sent to the assistant…
        Assert.Empty(_llm.Requests);
        Assert.Single(_bot.Requests.OfType<SendPhotoRequest>());
        UserSession session = await _store.GetAsync(UserId);
        Assert.True(session.HasImage);
        // …and chat mode survived the command.
        Assert.True(session.ChatMode);
    }

    [Fact]
    public async Task APendingFreeTextFlowWinsOverChatMode()
    {
        await Send("/blank");
        await Send("/chat");
        // The Rename button arms the pending free-text prompt (see CallbackAction).
        await Tap("name:menu");

        await Send("Poster draft");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("Poster draft", session.ImageLabel);
        Assert.Empty(_llm.Requests);
        // The one-shot flow is spent; the next plain message goes to the assistant again.
        Assert.Null(session.PendingInput);
        Assert.True(session.ChatMode);

        _llm.CannedReplies.Enqueue(new LlmReply("Nice name."));
        await Send("what do you think?");
        Assert.Single(_llm.Requests);
        Assert.Equal("Nice name.", Messages.Last().Text);
    }

    [Fact]
    public async Task ClearForgetsTheConversationWithoutLeavingChatMode()
    {
        await Send("/blank");
        await Send("/chat");
        await Send("first question");
        await Send("second question");
        // The second turn replayed the first exchange: user, assistant, user.
        Assert.Equal(3, _llm.Requests[^1].Messages.Count);

        await Send("/chat clear");

        Assert.Contains("Conversation cleared", Messages.Last().Text);
        Assert.Contains("Chat mode is still on", Messages.Last().Text);
        UserSession session = await _store.GetAsync(UserId);
        Assert.True(session.ChatMode);
        // The working image survives a clear — only the conversation is forgotten.
        Assert.True(session.HasImage);

        await Send("third question");

        LlmChatRequest fresh = _llm.Requests[^1];
        LlmMessage only = Assert.Single(fresh.Messages);
        Assert.Equal(LlmMessage.RoleUser, only.Role);
        Assert.Equal("third question", only.Text);
    }

    [Fact]
    public async Task TheClearButtonRunsTheSameClear()
    {
        await Send("/blank");
        await Send("/chat");
        await Send("first question");

        await Tap("chat:clear");

        Assert.Contains("Conversation cleared", Messages.Last().Text);
        await Send("next question");
        Assert.Single(_llm.Requests[^1].Messages);
        Assert.True((await _store.GetAsync(UserId)).ChatMode);
    }

    // ── §10 clearChat (the model asks; only the user's button clears) ──

    [Fact]
    public async Task AClearChatPlanDefersToAConfirmSentAfterTheReplyAndEdits()
    {
        await Send("/blank");
        await Send("/chat");
        await Send("first question");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"reply":"BW done.","actions":[{"op":"clearChat"},{"op":"filter","mode":"bw"}]}"""));

        await Send("make it bw and clear the chat");

        // The plan's edit ran regardless of clearChat's position…
        Assert.Equal("bw", _cli.LastRequest!.Filter);
        // …and the confirm is the LAST message of the turn, with the Yes/Cancel keyboard.
        SendMessageRequest confirm = Messages.Last();
        Assert.Contains("Clear it?", confirm.Text);
        InlineKeyboardMarkup markup = Assert.IsType<InlineKeyboardMarkup>(confirm.ReplyMarkup);
        Assert.Contains(markup.InlineKeyboard.SelectMany(r => r), b => b.CallbackData == "chatclear:confirm");
        Assert.Contains(markup.InlineKeyboard.SelectMany(r => r), b => b.CallbackData == "chatclear:cancel");
        // Nothing is forgotten until the user answers: the next turn replays every exchange
        // (2 prior exchanges = 4 messages, plus the current one).
        await Send("still here?");
        Assert.Equal(5, _llm.Requests[^1].Messages.Count);
    }

    [Fact]
    public async Task TheClearChatYesButtonRunsTheSameClearAsChatClear()
    {
        await Send("/blank");
        await Send("/chat");
        await Send("first question");
        _llm.CannedReplies.Enqueue(new LlmReply("""{"reply":"ok","actions":[{"op":"clearChat"}]}"""));
        await Send("clear this conversation");

        await Tap("chatclear:confirm");

        Assert.Contains("Conversation cleared", Messages.Last().Text);
        // The history really cleared — the next turn starts fresh — and chat mode survives.
        await Send("next question");
        Assert.Single(_llm.Requests[^1].Messages);
        Assert.True((await _store.GetAsync(UserId)).ChatMode);
    }

    [Fact]
    public async Task TheClearChatCancelButtonKeepsTheConversationWithACanceledNote()
    {
        await Send("/blank");
        await Send("/chat");
        await Send("first question");
        _llm.CannedReplies.Enqueue(new LlmReply("""{"reply":"ok","actions":[{"op":"clearChat"}]}"""));
        await Send("clear this conversation");

        await Tap("chatclear:cancel");

        // The decline is a "clear canceled" note (the confirm edited in place), never an error…
        EditMessageTextRequest note = _bot.Requests.OfType<EditMessageTextRequest>().Last();
        Assert.Contains("Clear canceled", note.Text);
        // …and nothing was forgotten: the next turn still replays every prior exchange.
        await Send("next question");
        Assert.Equal(5, _llm.Requests[^1].Messages.Count);
    }

    [Fact]
    public async Task DropAlsoForgetsTheConversation()
    {
        await Send("/blank");
        await Send("/chat");
        await Send("first question");

        await Send("/drop");

        Assert.False((await _store.GetAsync(UserId)).HasImage);
        await Send("a brand new question");
        LlmMessage only = Assert.Single(_llm.Requests[^1].Messages);
        Assert.Equal("a brand new question", only.Text);
    }

    [Fact]
    public async Task ChatModeIsPerUserAndShowsUpInStatus()
    {
        await Send("/chat");

        UserSession mine = await _store.GetAsync(UserId);
        UserSession other = await _store.GetAsync(UserId + 1);
        Assert.True(mine.ChatMode);
        Assert.False(other.ChatMode);
        Assert.Contains("Chat mode: on", Replies.StatusText(mine));
        Assert.DoesNotContain("Chat mode", Replies.StatusText(other));
    }
}
