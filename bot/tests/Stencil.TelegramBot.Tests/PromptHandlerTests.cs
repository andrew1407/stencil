using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The <c>/prompt</c> (and <c>/p</c>) handler through the real <see cref="CommandHandlers"/> +
/// <see cref="PromptService"/> + <see cref="EditingService"/>, with the LLM, CLI and Telegram
/// mocked: dispatch of both verbs, the usage hint, the reply + single-photo path, the
/// media-group path for variants, warning surfacing, and LLM failures shown as chat text.
/// </summary>
public sealed class PromptHandlerTests : IDisposable
{
    private const long UserId = 55;
    private const long ChatId = 66;

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly MockBotClient _bot = new();
    private readonly MockLlmClient _llm = new();
    private readonly InMemorySessionStore _store = new();
    private readonly CommandHandlers _handlers;
    private readonly CallbackAction _callbacks;

    public PromptHandlerTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-promptcmd-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir };
        _handlers = TestHandlers.Create(options, _store, _cli, _bot, _llm);
        _callbacks = new CallbackAction(_handlers, _bot, _store);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private Task Dispatch(string text) =>
        _handlers.DispatchAsync(UserId, ChatId, CommandParser.Parse(text), CancellationToken.None);

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

    [Fact]
    public async Task BarePromptSendsTheUsageHintAndNeverCallsTheLlm()
    {
        await Dispatch("/prompt");

        SendMessageRequest usage = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("Usage: /prompt", usage.Text);
        Assert.Empty(_llm.Requests);
    }

    [Fact]
    public async Task PromptWithAnActionPlanSendsTheReplyThenThePhoto()
    {
        await Dispatch("/blank");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"reply":"Made it black & white.","actions":[{"op":"filter","mode":"bw"}]}"""));

        await Dispatch("/prompt make it black and white");

        SendMessageRequest reply = _bot.Requests.OfType<SendMessageRequest>().Last();
        Assert.Equal("Made it black & white.", reply.Text);
        // /blank sent one photo; the prompt result is the second.
        Assert.Equal(2, _bot.Requests.OfType<SendPhotoRequest>().Count());
        Assert.Equal("bw", _cli.LastRequest!.Filter);
        // The working image (a rendered .png) rode along as the vision attachment.
        LlmChatRequest request = Assert.Single(_llm.Requests);
        Assert.Equal("image/png", Assert.Single(request.Messages[^1].Images).MediaType);
    }

    [Fact]
    public async Task ShortAliasPDispatchesToo()
    {
        await Dispatch("/blank");
        _llm.CannedReplies.Enqueue(new LlmReply("Just chatting."));

        await Dispatch("/p hello there");

        Assert.Single(_llm.Requests);
        SendMessageRequest reply = _bot.Requests.OfType<SendMessageRequest>().Last();
        Assert.Equal("Just chatting.", reply.Text);
        // Chat-only: no photo beyond the /blank render.
        Assert.Single(_bot.Requests.OfType<SendPhotoRequest>());
    }

    [Fact]
    public async Task VariantsGoOutAsOneMediaGroup()
    {
        await Dispatch("/blank");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """
            {"reply":"Two takes.","actions":[],"variants":[
              {"label":"rotated","actions":[{"op":"rotate","dir":"left"}]},
              {"label":"sepia","actions":[{"op":"filter","mode":"sepia"}]}]}
            """));

        await Dispatch("/prompt two variants please");

        SendMediaGroupRequest album = Assert.Single(_bot.Requests.OfType<SendMediaGroupRequest>());
        Assert.Equal(2, album.Media.Count());
    }

    [Fact]
    public async Task UnknownOpWarningIsAppendedToTheReply()
    {
        await Dispatch("/blank");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"reply":"Done.","actions":[{"op":"sharpen"},{"op":"filter","mode":"bw"}]}"""));

        await Dispatch("/prompt sharpen and bw");

        SendMessageRequest reply = _bot.Requests.OfType<SendMessageRequest>().Last();
        Assert.Contains("Done.", reply.Text);
        Assert.Contains("sharpen", reply.Text);
        Assert.Contains("🟡", reply.Text);
    }

    [Fact]
    public async Task LlmFailuresAreShownAsChatTextNotCrashes()
    {
        await Dispatch("/blank");
        _llm.Throw = new LlmException("The AI response was cut off at the token limit — try a shorter or simpler request.", LlmFailure.Truncated);

        await Dispatch("/prompt do everything");

        SendMessageRequest reply = _bot.Requests.OfType<SendMessageRequest>().Last();
        Assert.Contains("cut off at the token limit", reply.Text);
        // No render was attempted beyond the /blank one.
        Assert.Single(_bot.Requests.OfType<SendPhotoRequest>());
    }

    [Fact]
    public async Task AFailedTurnOffersRetryAndTheButtonReRunsTheSamePrompt()
    {
        await Dispatch("/blank");
        _llm.Throw = new LlmException("The AI service timed out.");

        await Dispatch("/prompt make it sepia");

        SendMessageRequest failure = _bot.Requests.OfType<SendMessageRequest>().Last();
        Assert.Contains("timed out", failure.Text);
        InlineKeyboardMarkup keyboard = Assert.IsType<InlineKeyboardMarkup>(failure.ReplyMarkup);
        Assert.Equal("retry:prompt", keyboard.InlineKeyboard.Single().Single().CallbackData);
        Assert.Equal("make it sepia", (await _store.GetAsync(UserId)).LastRetryablePrompt);

        // Tapping Retry re-runs the stored turn through the normal prompt path…
        _llm.Throw = null;
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"reply":"Sepia it is.","actions":[{"op":"filter","mode":"sepia"}]}"""));
        await Tap("retry:prompt");

        Assert.Equal(2, _llm.Requests.Count);
        Assert.Contains("make it sepia", _llm.Requests[^1].Messages[^1].Text);
        Assert.Equal("sepia", _cli.LastRequest!.Filter);
        // …and the answered turn leaves nothing for a second tap to re-send.
        Assert.Null((await _store.GetAsync(UserId)).LastRetryablePrompt);
    }

    [Fact]
    public async Task RetryOnAnAlreadyAnsweredTurnSaysSoInsteadOfResending()
    {
        await Dispatch("/blank");

        await Tap("retry:prompt");

        Assert.Contains("no longer pending", _bot.Requests.OfType<SendMessageRequest>().Last().Text);
        Assert.Empty(_llm.Requests);
    }

    [Fact]
    public async Task ARefusalCarriesNoRetryButton()
    {
        await Dispatch("/blank");
        _llm.Throw = new LlmException("The AI declined to answer that.", LlmFailure.Refusal);

        await Dispatch("/prompt do something forbidden");

        SendMessageRequest failure = _bot.Requests.OfType<SendMessageRequest>().Last();
        Assert.Contains("declined", failure.Text);
        // Re-sending a refusal verbatim would just repeat it, so there is nothing to retry.
        Assert.Null(failure.ReplyMarkup);
        Assert.Null((await _store.GetAsync(UserId)).LastRetryablePrompt);
    }

    [Fact]
    public async Task TheWaitNoticeGoesOutBeforeTheTurnAndIsGoneBeforeTheReply()
    {
        await Dispatch("/blank");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"reply":"Made it black & white.","actions":[{"op":"filter","mode":"bw"}]}"""));
        int before = _bot.Requests.Count;

        await Dispatch("/prompt make it black and white");

        List<object> turn = _bot.Requests.Skip(before).ToList();
        // First out is the spinning notice — the user is told to wait before any work starts…
        SendMessageRequest notice = Assert.IsType<SendMessageRequest>(turn[0]);
        Assert.Equal(ProgressNotice.Frame(0, Replies.PromptWorking()), notice.Text);
        // …and it is removed before the reply, so only the answer is left in the chat.
        int removed = turn.FindIndex(r => r is DeleteMessageRequest);
        int reply = turn.FindIndex(r => r is SendMessageRequest m && m.Text.Contains("black & white"));
        Assert.InRange(removed, 1, reply - 1);
    }

    [Fact]
    public async Task TheWaitNoticeIsRemovedWhenTheTurnFailsToo()
    {
        await Dispatch("/blank");
        _llm.Throw = new LlmException("The AI service timed out.", LlmFailure.Error);
        int before = _bot.Requests.Count;

        await Dispatch("/prompt do everything");

        List<object> turn = _bot.Requests.Skip(before).ToList();
        int removed = turn.FindIndex(r => r is DeleteMessageRequest);
        int error = turn.FindIndex(r => r is SendMessageRequest m && m.Text.Contains("timed out"));
        Assert.InRange(removed, 1, error - 1); // no spinner left hanging over the failure
    }

    [Fact]
    public async Task ExportActionsSendOneDocumentEachIntoTheChat()
    {
        await Dispatch("/blank");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"reply":"Here you go.","actions":[{"op":"export","what":"layout"},{"op":"export","what":"project"}]}"""));

        await Dispatch("/prompt send me the layout json and the project file");

        // §10 export: exactly one document send per action — the same /json and /project shapes.
        List<SendDocumentRequest> documents = _bot.Requests.OfType<SendDocumentRequest>().ToList();
        Assert.Equal(2, documents.Count);
        Assert.Equal("Layout JSON", documents[0].Caption);
        Assert.Equal("Stencil project", documents[1].Caption);
        // Exports change no pixels: only the /blank photo was ever sent.
        Assert.Single(_bot.Requests.OfType<SendPhotoRequest>());
    }

    [Fact]
    public async Task ClearPlanSendsNoPhotoAndTheConversationSurvivesIt()
    {
        await Dispatch("/blank");
        _llm.CannedReplies.Enqueue(new LlmReply("Nice blank!"));
        await Dispatch("/prompt what do you think?");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"reply":"Removed the image.","actions":[{"op":"clear"}]}"""));

        await Dispatch("/prompt remove the image");

        // No render followed the clear (there is nothing left to render)…
        Assert.Single(_bot.Requests.OfType<SendPhotoRequest>()); // the /blank one only
        SendMessageRequest reply = _bot.Requests.OfType<SendMessageRequest>().Last();
        Assert.Equal("Removed the image.", reply.Text);
        // …and the assistant conversation SURVIVED the clear (unlike /drop): the next turn
        // still replays the two earlier exchanges (4 messages) plus the current one.
        _llm.CannedReplies.Enqueue(new LlmReply("Still here."));
        await Dispatch("/prompt are you still there?");
        Assert.Equal(5, _llm.Requests[^1].Messages.Count);
    }
}
