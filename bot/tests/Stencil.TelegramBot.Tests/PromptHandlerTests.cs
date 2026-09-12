using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The <c>/prompt</c> (and <c>/p</c>) main path: both verbs, the usage hint, reply + photo,
/// the media group for variants, warning surfacing, and LLM failures as chat text.
/// </summary>
public sealed class PromptHandlerTests : PromptHandlerTestBase
{
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
        _llm.Throw = new LlmException("The AI response was cut off at the token limit — try a shorter or simpler request.", LlmFailure.TRUNCATED);

        await Dispatch("/prompt do everything");

        SendMessageRequest reply = _bot.Requests.OfType<SendMessageRequest>().Last();
        Assert.Contains("cut off at the token limit", reply.Text);
        // No render was attempted beyond the /blank one.
        Assert.Single(_bot.Requests.OfType<SendPhotoRequest>());
    }
}
