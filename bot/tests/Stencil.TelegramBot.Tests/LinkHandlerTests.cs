using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The <c>/link</c> handler: the desktop hand-off link for the active server project. It reads
/// the session and the operator's browser-app base only — a <see cref="ThrowingServerService"/>
/// fails the test loudly if it ever calls the server for a link.
/// </summary>
public sealed class LinkHandlerTests : IDisposable
{
    private const long UserId = 91;
    private const long ChatId = 92;

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

    private CommandHandlers Handlers(string? browserAppUrl) =>
        TestHandlers.Create(
            browserAppUrl is null
                ? new BotOptions { DataDir = _dataDir }                                  // the default base
                : new BotOptions { DataDir = _dataDir, BrowserAppUrl = browserAppUrl },
            _store, new MockStencilCli(), _bot);

    private Task Dispatch(string? browserAppUrl, string text = "/link") =>
        Handlers(browserAppUrl).DispatchAsync(UserId, ChatId, CommandParser.Parse(text), CancellationToken.None);

    private async Task SeedActiveProject()
    {
        UserSession session = await _store.GetAsync(UserId);
        await _store.SaveAsync(session with
        {
            ActiveServerUrl = "http://localhost:8090",
            ActiveProjectId = "p_1a2b3c_x1",
            ActiveProjectName = "Poster",
            ActiveProjectVersion = 7,
        });
    }

    [Fact]
    public async Task SendsTheBouncedSchemeUrlForTheActiveProject()
    {
        await SeedActiveProject();

        await Dispatch("https://stencil.example/app/");

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
    public async Task WithoutAServerProjectAsksForOne()
    {
        await Dispatch("https://stencil.example/app");

        SendMessageRequest reply = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("No active server project", reply.Text);
        Assert.DoesNotContain("launch.html", reply.Text);
    }

    [Fact]
    public async Task UnconfiguredFallsBackToTheDevServerAndFlagsItAsLocalOnly()
    {
        await SeedActiveProject();

        await Dispatch(browserAppUrl: null);   // BotOptions default: http://localhost:8080

        SendMessageRequest reply = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("http://localhost:8080/launch.html#stencil-desktop=", reply.Text);
        // A loopback link opens on whoever taps it, so the reply must not read as shareable.
        Assert.Contains("only works on this machine", reply.Text);
    }

    [Fact]
    public async Task ARemoteBrowserAppCarriesNoLocalOnlyCaveat()
    {
        await SeedActiveProject();

        await Dispatch("https://stencil.example/app");

        SendMessageRequest reply = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.DoesNotContain("only works on this machine", reply.Text);
    }

    [Fact]
    public async Task AnUnusableConfiguredBaseSaysSoInsteadOfSendingABrokenLink()
    {
        await SeedActiveProject();

        await Dispatch("not-a-url");

        SendMessageRequest reply = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("STENCIL_BOT_BROWSER_URL", reply.Text);
        Assert.DoesNotContain("#stencil-desktop=", reply.Text);
    }

    [Fact]
    public async Task TheAliasesAndTheKeyboardTokenReachTheSameHandler()
    {
        await SeedActiveProject();

        foreach (string text in new[] { "/link", "/desktop", "/open-in", "/openin" })
        {
            await Dispatch("https://stencil.example/app", text);
        }

        List<SendMessageRequest> replies = _bot.Requests.OfType<SendMessageRequest>().ToList();
        Assert.Equal(4, replies.Count);
        Assert.All(replies, r => Assert.Contains("launch.html#stencil-desktop=", r.Text));
    }
}
