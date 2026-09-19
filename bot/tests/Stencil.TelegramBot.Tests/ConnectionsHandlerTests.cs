using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;

namespace Stencil.TelegramBot.Tests;

/// <summary>The <c>/connections</c> handler — bare listing, <c>admin</c>/<c>session</c> credential filters, usage reply — over the real <see cref="CommandHandlers"/> and a <see cref="RecordingServerService"/>.</summary>
public sealed class ConnectionsHandlerTests : IDisposable
{
    private const long _userId = 91;
    private const long _chatId = 92;

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
                CredentialKind = CredentialKind.ADMIN,
            },
            new ServerConnectionInfo { Url = "http://b:8090", Token = "sess-b", CredentialKind = CredentialKind.NONE },
            new ServerConnectionInfo
            {
                Url = "http://c:8090",
                Token = "sess-c",
                Credential = "sess-c",
                CredentialKind = CredentialKind.SESSION,
            },
        ];
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private Task dispatch(string text) =>
        _handlers.DispatchAsync(_userId, _chatId, CommandParser.Parse(text), CancellationToken.None);

    private string lastText() => _bot.Requests.OfType<SendMessageRequest>().Last().Text;

    [Fact]
    public async Task Should_List_Them_All_And_Mark_The_Admin_One_Without_Its_Token_On_A_Bare_Command()
    {
        await dispatch("/connections");

        string text = lastText();
        Assert.Contains("Connections (3):", text);
        Assert.Contains("http://a:8090 [admin]", text);
        Assert.Contains("http://b:8090", text);
        Assert.Contains("http://c:8090", text);
        Assert.DoesNotContain("adm-secret", text); // never print a credential
        Assert.DoesNotContain("sess-", text);
    }

    [Fact]
    public async Task Should_Keep_Only_The_Admin_Credential_Connections_For_The_Admin_Filter()
    {
        await dispatch("/connections admin");

        string text = lastText();
        Assert.Contains("Admin-token connections (1):", text);
        Assert.Contains("http://a:8090 [admin]", text);
        Assert.DoesNotContain("http://b:8090", text);
        Assert.DoesNotContain("http://c:8090", text);
    }

    [Fact]
    public async Task Should_Keep_Everything_That_Is_Not_An_Admin_Credential_For_The_Session_Filter()
    {
        await dispatch("/connections SESSION"); // case-insensitive, like the other arguments

        string text = lastText();
        Assert.Contains("Session-token connections (2):", text);
        Assert.Contains("http://b:8090", text);
        Assert.Contains("http://c:8090", text);
        Assert.DoesNotContain("http://a:8090", text);
    }

    [Fact]
    public async Task Should_Say_So_Instead_Of_The_Connect_Hint_When_Filtering_Out_Everything()
    {
        _servers.Connections = [new ServerConnectionInfo { Url = "http://b:8090", CredentialKind = CredentialKind.NONE }];

        await dispatch("/connections admin");

        Assert.Contains("No admin-token connections.", lastText());
    }

    [Fact]
    public async Task Should_Reply_With_Usage_And_List_Nothing_For_An_Unknown_Argument()
    {
        await dispatch("/connections everything");

        string text = lastText();
        Assert.Contains("/connections admin", text);
        Assert.Contains("/connections session", text);
        Assert.DoesNotContain("http://a:8090", text);
    }
}
