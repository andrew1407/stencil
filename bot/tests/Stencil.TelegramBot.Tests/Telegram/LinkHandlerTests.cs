using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Stencil.TelegramBot.Bot.Telegram.Commands;

namespace Stencil.TelegramBot.Tests.Telegram;

/// <summary>The <c>/link</c> handler builds the desktop hand-off link from the session and the operator's browser-app base only — a <see cref="ThrowingServerService"/> fails the test loudly if it calls the server.</summary>
public sealed class LinkHandlerTests : IDisposable
{
    private const long _userId = 91;
    private const long _chatId = 92;

    private readonly string _dataDir;
    private readonly MockBotClient _bot = new();
    private readonly InMemorySessionStore _store = new();

    public LinkHandlerTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-link-" + Guid.NewGuid().ToString("N"));
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private CommandHandlers handlers(string? browserAppUrl) =>
        TestHandlers.Create(
            browserAppUrl is null
                ? new BotOptions { DataDir = _dataDir }                                  // the default base
                : new BotOptions { DataDir = _dataDir, BrowserAppUrl = browserAppUrl },
            _store, new MockStencilCli(), _bot);

    private Task dispatch(string? browserAppUrl, string text = "/link") =>
        handlers(browserAppUrl).DispatchAsync(_userId, _chatId, CommandParser.Parse(text), CancellationToken.None);

    private async Task seedActiveProject()
    {
        UserSession session = await _store.GetAsync(_userId);
        await _store.SaveAsync(session with
        {
            ActiveServerUrl = "http://localhost:8090",
            ActiveProjectId = "p_1a2b3c_x1",
            ActiveProjectName = "Poster",
            ActiveProjectVersion = 7,
        });
    }

    [Fact]
    public async Task Should_Send_The_Bounced_Scheme_Url_For_The_Active_Project()
    {
        await seedActiveProject();

        await dispatch("https://stencil.example/app/");

        SendMessageRequest reply = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("Poster", reply.Text);
        Assert.Contains(
            "https://stencil.example/app/launch.html#stencil-desktop="
            + "stencil%3A%2F%2Fopen%3Fserver%3Dhttp%253A%252F%252Flocalhost%253A8090"
            + "%26id%3Dp_1a2b3c_x1%26version%3D7",
            reply.Text);
        // No credential ever rides the link — the recipient connects with their own.
        Assert.DoesNotContain("token=", reply.Text);
    }

    [Fact]
    public async Task Should_Ask_For_A_Server_Project_When_There_Is_None()
    {
        await dispatch("https://stencil.example/app");

        SendMessageRequest reply = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("No active server project", reply.Text);
        Assert.DoesNotContain("launch.html", reply.Text);
    }

    [Fact]
    public async Task Should_Fall_Back_To_The_Dev_Server_And_Flag_It_As_Local_Only_When_Unconfigured()
    {
        await seedActiveProject();

        await dispatch(browserAppUrl: null);   // BotOptions default: http://localhost:8080

        SendMessageRequest reply = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("http://localhost:8080/launch.html#stencil-desktop=", reply.Text);
        // A loopback link opens on whoever taps it, so the reply must not read as shareable.
        Assert.Contains("only works on this machine", reply.Text);
    }

    [Fact]
    public async Task Should_Carry_No_Local_Only_Caveat_For_A_Remote_Browser_App()
    {
        await seedActiveProject();

        await dispatch("https://stencil.example/app");

        SendMessageRequest reply = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.DoesNotContain("only works on this machine", reply.Text);
    }

    [Fact]
    public async Task Should_Say_So_Instead_Of_Sending_A_Broken_Link_For_An_Unusable_Configured_Base()
    {
        await seedActiveProject();

        await dispatch("not-a-url");

        SendMessageRequest reply = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("STENCIL_BOT_BROWSER_URL", reply.Text);
        Assert.DoesNotContain("#stencil-desktop=", reply.Text);
    }

    [Fact]
    public async Task Should_Reach_The_Same_Handler_From_The_Aliases_And_The_Keyboard_Token()
    {
        await seedActiveProject();

        foreach (string text in new[] { "/link", "/desktop", "/open-in", "/openin" })
        {
            await dispatch("https://stencil.example/app", text);
        }

        List<SendMessageRequest> replies = _bot.Requests.OfType<SendMessageRequest>().ToList();
        Assert.Equal(4, replies.Count);
        Assert.All(replies, r => Assert.Contains("launch.html#stencil-desktop=", r.Text));
    }
}
