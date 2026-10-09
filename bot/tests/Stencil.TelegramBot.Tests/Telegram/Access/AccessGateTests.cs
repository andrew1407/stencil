using Stencil.TelegramBot.Bot.Telegram.Access;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Requests.Abstractions;

namespace Stencil.TelegramBot.Tests.Telegram.Access;

/// <summary>The refusal throttle on its own: a send never holds the polling loop, the replies across
/// every stranger are capped per window, and a full map evicts rather than resetting every throttle.</summary>
public sealed class AccessGateTests
{
    private readonly AccessAdmissionTests.ManualClock _clock = new();
    private readonly MockLogger<AccessGate> _log = new();

    private AccessGate gateFor(MockBotClient bot) =>
        new(new BotOptions { AllowedUsers = new HashSet<long> { 1 }, RefusalReplyWindow = TimeSpan.FromMinutes(5) },
            bot, _log, _clock);

    private static int refusals(MockBotClient bot) => bot.Requests.OfType<SendMessageRequest>().Count();

    private sealed class HangingBot : MockBotClient
    {
        public override Task<TResponse> SendRequest<TResponse>(IRequest<TResponse> request, CancellationToken ct = default)
        {
            base.SendRequest(request, ct);
            return new TaskCompletionSource<TResponse>().Task;
        }
    }

    [Fact]
    public void Should_Answer_Without_Waiting_For_The_Refusal_Send()
    {
        HangingBot bot = new();

        Assert.False(gateFor(bot).Allows(999, 66, CancellationToken.None));

        Assert.Equal(1, refusals(bot));
    }

    [Fact]
    public void Should_Cap_Refusal_Replies_Across_Every_Stranger_Per_Window()
    {
        MockBotClient bot = new();
        AccessGate gate = gateFor(bot);

        for (long id = 1000; id < 1000 + AccessGate.MaxRefusalsPerWindow + 20; id++)
        {
            Assert.False(gate.Allows(id, id, CancellationToken.None));
        }
        Assert.Equal(AccessGate.MaxRefusalsPerWindow, refusals(bot));
        Assert.Equal(AccessGate.MaxRefusalsPerWindow + 20, _log.Entries.Count);

        _clock.Now += TimeSpan.FromMinutes(6);
        gate.Allows(5000, 5000, CancellationToken.None);
        Assert.Equal(AccessGate.MaxRefusalsPerWindow + 1, refusals(bot));
    }

    [Fact]
    public void Should_Evict_Only_The_Oldest_Id_When_The_Map_Is_Full()
    {
        AccessGate gate = gateFor(new MockBotClient());
        for (long id = 1000; id < 1000 + AccessGate.MaxRefusalsTracked; id++)
        {
            gate.Allows(id, id, CancellationToken.None);
            _clock.Now += TimeSpan.FromMilliseconds(1);
        }

        gate.Allows(5000, 5000, CancellationToken.None);
        for (long id = 1001; id < 1000 + AccessGate.MaxRefusalsTracked; id++)
        {
            gate.Allows(id, id, CancellationToken.None);
        }

        Assert.Equal(AccessGate.MaxRefusalsTracked + 1, _log.Entries.Count);
        gate.Allows(1000, 1000, CancellationToken.None);
        Assert.Equal(AccessGate.MaxRefusalsTracked + 2, _log.Entries.Count);
    }
}
