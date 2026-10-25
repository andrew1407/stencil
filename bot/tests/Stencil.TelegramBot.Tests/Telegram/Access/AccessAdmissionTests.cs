using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Bot.Telegram.Access;
using Stencil.TelegramBot.Bot.Telegram.Messaging;
using Stencil.TelegramBot.Bot.Telegram.Sync;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Tests.Telegram.Access;

/// <summary>The allowlist asked on the polling loop, before the pump: an unlisted sender is refused
/// without a lane, answered at most once per <c>RefusalReplyWindow</c>, and bare <c>/start</c> and
/// <c>/help</c> are still admitted.</summary>
public sealed class AccessAdmissionTests : IDisposable
{
    private const long _allowed = 55;
    private const long _stranger = 999;
    private const long _chatId = 66;

    private readonly string _dataDir =
        TempDirs.New("bot-admission");
    private readonly MockStencilCli _cli = new();
    private readonly MockBotClient _bot = new();
    private readonly InMemorySessionStore _store = new();
    private readonly ManualClock _clock = new();

    public void Dispose()
    {
        TempDirs.Delete(_dataDir);
    }

    internal sealed class ManualClock : TimeProvider
    {
        public DateTimeOffset Now { get; set; } = new(2026, 1, 1, 0, 0, 0, TimeSpan.Zero);

        public override DateTimeOffset GetUtcNow() => Now;
    }

    internal static UpdateRouter RouterFor(
        string dataDir, InMemorySessionStore store, MockStencilCli cli, MockBotClient bot, TimeProvider? clock,
        SyncRegistry? sync, params long[] allowed)
    {
        BotOptions options = new()
        {
            DataDir = dataDir, AllowedUsers = new HashSet<long>(allowed), RefusalReplyWindow = TimeSpan.FromMinutes(5),
        };
        EditingService editing = new(cli, new UserWorkspace(options), store);
        var handlers = TestHandlers.Create(options, store, cli, bot, editing: editing);
        return new UpdateRouter(handlers, new CallbackAction(handlers, bot, store), editing, store, bot,
            new UserGate(), options, new MockLogger<UpdateRouter>(), clock: clock, sync: sync);
    }

    internal static Message TextFrom(long userId, string text) =>
        new() { Chat = new Chat { Id = _chatId }, From = new User { Id = userId }, Text = text };

    private readonly SyncRegistry _sync = new();

    private UpdateRouter router() => RouterFor(_dataDir, _store, _cli, _bot, _clock, _sync, _allowed);

    private int refusals() =>
        _bot.Requests.OfType<SendMessageRequest>().Count(m => m.Text == Replies.AccessRefused(closed: false));

    [Fact]
    public void Should_Refuse_A_Stranger_Once_Per_Window()
    {
        UpdateRouter gate = router();

        for (int i = 0; i < 50; i++)
        {
            Assert.False(gate.Admit(TextFrom(_stranger, "/crop x1=10%"), CancellationToken.None));
        }
        Assert.Equal(1, refusals());

        _clock.Now += TimeSpan.FromMinutes(4);
        Assert.False(gate.Admit(TextFrom(_stranger, "/blank"), CancellationToken.None));
        Assert.Equal(1, refusals());

        _clock.Now += TimeSpan.FromMinutes(2); // past the window since the reply
        Assert.False(gate.Admit(TextFrom(_stranger, "/blank"), CancellationToken.None));
        Assert.Equal(2, refusals());
    }

    [Fact]
    public void Should_Refuse_A_Strangers_Taps_Within_The_Same_Window()
    {
        UpdateRouter gate = router();
        CallbackQuery tap = new()
        {
            Id = "q1", From = new User { Id = _stranger }, Data = "bw",
            Message = new Message { Id = 2, Chat = new Chat { Id = _chatId } },
        };

        Assert.False(gate.Admit(tap, CancellationToken.None));
        Assert.False(gate.Admit(TextFrom(_stranger, "/blank"), CancellationToken.None));

        Assert.Equal(1, refusals());
    }

    [Theory]
    [InlineData("/start")]
    [InlineData("/help")]
    public async Task Should_Admit_A_Strangers_Bare_Start_And_Help(string command)
    {
        UpdateRouter gate = router();

        Assert.True(gate.Admit(TextFrom(_stranger, command), CancellationToken.None));
        Assert.Empty(_bot.Requests);
        await gate.HandleMessageAsync(TextFrom(_stranger, command), CancellationToken.None);
        Assert.NotEmpty(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Equal(0, refusals());
    }

    [Fact]
    public async Task Should_Track_No_Sync_State_For_A_Stranger_Who_Only_Asks_For_Help()
    {
        UpdateRouter gate = router();

        await gate.HandleMessageAsync(TextFrom(_stranger, "/help"), CancellationToken.None);
        await gate.HandleMessageAsync(TextFrom(_allowed, "/help"), CancellationToken.None);

        // Every id is kept for the process's life, so a stranger's must never be added.
        Assert.False(_sync.Consulted(_stranger));
        Assert.True(_sync.Consulted(_allowed));
    }

    [Fact]
    public void Should_Admit_A_Listed_User_Without_A_Reply()
    {
        Assert.True(router().Admit(TextFrom(_allowed, "/crop x1=10%"), CancellationToken.None));
        Assert.Empty(_bot.Requests);
    }
}
