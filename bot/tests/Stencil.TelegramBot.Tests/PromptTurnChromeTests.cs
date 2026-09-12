using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// What a turn leaves behind: the 🔄 Retry button (absent on a refusal), the working notice's
/// lifetime, exported documents, and a plan that sends no photo at all.
/// </summary>
public sealed class PromptTurnChromeTests : PromptHandlerTestBase
{
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
        _llm.Throw = new LlmException("The AI declined to answer that.", LlmFailure.REFUSAL);

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
        _llm.Throw = new LlmException("The AI service timed out.", LlmFailure.ERROR);
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
