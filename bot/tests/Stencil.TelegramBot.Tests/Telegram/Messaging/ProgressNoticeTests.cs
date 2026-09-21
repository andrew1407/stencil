using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types.Enums;
using Stencil.TelegramBot.Bot.Telegram.Messaging;

namespace Stencil.TelegramBot.Tests.Telegram.Messaging;

/// <summary>The spinning "still working" notice: what it posts, what it removes, and that neither a second stop nor a Telegram that never returned a message can make it misbehave.</summary>
public sealed class ProgressNoticeTests
{
    private const long _chatId = 42;

    [Fact]
    public async Task Should_Post_The_First_Frame_Immediately_On_Start_And_Remove_It_On_Stop()
    {
        MockBotClient bot = new();

        ProgressNotice notice = await ProgressNotice.StartAsync(
            bot, _chatId, "Working on your request…", ChatAction.Typing, CancellationToken.None);

        SendMessageRequest sent = Assert.Single(bot.Requests.OfType<SendMessageRequest>());
        Assert.Equal("◐ Working on your request…", sent.Text);
        Assert.Empty(bot.Requests.OfType<DeleteMessageRequest>()); // still up while the turn runs

        await notice.StopAsync();

        DeleteMessageRequest removed = Assert.Single(bot.Requests.OfType<DeleteMessageRequest>());
        Assert.Equal(1, removed.MessageId); // the notice's own message, not the user's
    }

    [Fact]
    public async Task Should_Remove_It_Once_When_Stopped_Twice()
    {
        MockBotClient bot = new();
        ProgressNotice notice = await ProgressNotice.StartAsync(
            bot, _chatId, "Working…", ChatAction.Typing, CancellationToken.None);

        await notice.StopAsync();
        await notice.StopAsync(); // a catch path may stop it before the finally does

        Assert.Single(bot.Requests.OfType<DeleteMessageRequest>());
    }

    [Fact]
    public async Task Should_Leave_The_Turn_Alone_And_Delete_Nothing_When_The_Send_Fails()
    {
        ThrowingBotClient bot = new();

        ProgressNotice notice = await ProgressNotice.StartAsync(
            bot, _chatId, "Working…", ChatAction.Typing, CancellationToken.None);
        await notice.StopAsync();

        Assert.Empty(bot.Requests.OfType<DeleteMessageRequest>());
    }

    [Fact]
    public void Should_Cycle_The_Frame_Through_Every_Spinner_Position()
    {
        string[] cycle = [.. Enumerable.Range(0, ProgressNotice.Frames.Length)
            .Select(i => ProgressNotice.Frame(i, "x"))];

        Assert.Equal(ProgressNotice.Frames.Length, cycle.Distinct().Count());
        Assert.Equal(cycle[0], ProgressNotice.Frame(ProgressNotice.Frames.Length, "x")); // wraps
        Assert.Equal("◐ x", cycle[0]);
    }

    /// <summary>A Telegram that refuses the notice send — the turn must not notice.</summary>
    private sealed class ThrowingBotClient : MockBotClient
    {
        public override Task<TResponse> SendRequest<TResponse>(
            global::Telegram.Bot.Requests.Abstractions.IRequest<TResponse> request,
            CancellationToken cancellationToken = default)
        {
            if (request is SendMessageRequest)
            {
                throw new InvalidOperationException("telegram is having a day");
            }
            return base.SendRequest(request, cancellationToken);
        }
    }
}
