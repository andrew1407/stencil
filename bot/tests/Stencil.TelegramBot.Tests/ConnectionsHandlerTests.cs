using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The <c>/connections</c> handler: the bare listing, the <c>admin</c>/<c>session</c> credential
/// filters, and the usage reply for anything else. Runs the real <see cref="CommandHandlers"/>
/// over a <see cref="RecordingServerService"/> holding the remembered connections.
/// </summary>
public sealed class ConnectionsHandlerTests : IDisposable
{
    private const long UserId = 91;
    private const long ChatId = 92;

    private readonly string _dataDir;
    private readonly MockBotClient _bot = new();
    private readonly RecordingServerService _servers = new();
    private readonly CommandHandlers _handlers;

    public ConnectionsHandlerTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-conn-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir };
        _handlers = TestHandlers.Create(options, new InMemorySessionStore(), new MockStencilCli(), _bot, servers: _servers);
        _servers.Connections =
        [
            new ServerConnectionInfo
            {
                Url = "http://a:8090",
                Token = "sess-a",
                Credential = "adm-secret",
                CredentialKind = CredentialKind.Admin,
            },
            new ServerConnectionInfo { Url = "http://b:8090", Token = "sess-b", CredentialKind = CredentialKind.None },
            new ServerConnectionInfo
            {
                Url = "http://c:8090",
                Token = "sess-c",
                Credential = "sess-c",
                CredentialKind = CredentialKind.Session,
            },
        ];
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private Task Dispatch(string text) =>
        _handlers.DispatchAsync(UserId, ChatId, CommandParser.Parse(text), CancellationToken.None);

    private string LastText() => _bot.Requests.OfType<SendMessageRequest>().Last().Text;

    [Fact]
    public async Task BareCommandListsThemAllAndMarksTheAdminOneWithoutItsToken()
    {
        await Dispatch("/connections");

        string text = LastText();
        Assert.Contains("Connections (3):", text);
        Assert.Contains("http://a:8090 [admin]", text);
        Assert.Contains("http://b:8090", text);
        Assert.Contains("http://c:8090", text);
        Assert.DoesNotContain("adm-secret", text); // never print a credential
        Assert.DoesNotContain("sess-", text);
    }

    [Fact]
    public async Task AdminFilterKeepsOnlyTheAdminCredentialConnections()
    {
        await Dispatch("/connections admin");

        string text = LastText();
        Assert.Contains("Admin-token connections (1):", text);
        Assert.Contains("http://a:8090 [admin]", text);
        Assert.DoesNotContain("http://b:8090", text);
        Assert.DoesNotContain("http://c:8090", text);
    }

    [Fact]
    public async Task SessionFilterKeepsEverythingThatIsNotAnAdminCredential()
    {
        await Dispatch("/connections SESSION"); // case-insensitive, like the other arguments

        string text = LastText();
        Assert.Contains("Session-token connections (2):", text);
        Assert.Contains("http://b:8090", text);
        Assert.Contains("http://c:8090", text);
        Assert.DoesNotContain("http://a:8090", text);
    }

    [Fact]
    public async Task FilteringOutEverythingSaysSoInsteadOfTheConnectHint()
    {
        _servers.Connections = [new ServerConnectionInfo { Url = "http://b:8090", CredentialKind = CredentialKind.None }];

        await Dispatch("/connections admin");

        Assert.Contains("No admin-token connections.", LastText());
    }

    [Fact]
    public async Task UnknownArgumentRepliesWithUsageAndListsNothing()
    {
        await Dispatch("/connections everything");

        string text = LastText();
        Assert.Contains("/connections admin", text);
        Assert.Contains("/connections session", text);
        Assert.DoesNotContain("http://a:8090", text);
    }
}
