using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The bot's global fail-closed allowlist (<c>STENCIL_BOT_ALLOWED_USERS</c>), driven through the
/// real <see cref="UpdateRouter"/>. Every command, upload and button spends the operator's
/// resources — a CLI process, disk, an outbound fetch, the LLM key — so an unlisted user gets
/// <c>/start</c> and <c>/help</c> and nothing else, and an empty list turns the bot off for
/// everyone. The refusal the user reads is one plain sentence; the env var, the procedure and the
/// caller's id are operator detail and go to the log.
/// </summary>
public sealed class AllowlistTests : IDisposable
{
    private const long Allowed = 55;
    private const long Stranger = 999;
    private const long ChatId = 66;

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly MockBotClient _bot = new();
    private readonly MockLlmClient _llm = new();
    private readonly MockLogger<UpdateRouter> _log = new();
    private readonly InMemorySessionStore _store = new();

    public AllowlistTests() =>
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-allowlist-" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private UpdateRouter routerFor(params long[] allowed)
    {
        BotOptions options = new() { DataDir = _dataDir, AllowedUsers = new HashSet<long>(allowed) };
        EditingService editing = new(_cli, new UserWorkspace(options), _store);
        CommandHandlers handlers = TestHandlers.Create(options, _store, _cli, _bot, _llm, editing: editing);
        return new UpdateRouter(
            handlers, new CallbackAction(handlers, _bot, _store), editing, _store, _bot,
            new UserGate(), options, _log);
    }

    private static Message textFrom(long userId, string text) =>
        new() { Chat = new Chat { Id = ChatId }, From = new User { Id = userId }, Text = text };

    private static Message photoFrom(long userId) =>
        new()
        {
            Id = 1,
            Chat = new Chat { Id = ChatId },
            From = new User { Id = userId },
            Photo = [new PhotoSize { FileId = "f1", FileUniqueId = "f1", Width = 90, Height = 90 }],
        };

    private static string lastText(MockBotClient bot) =>
        bot.Requests.OfType<SendMessageRequest>().Last().Text;

    // Nothing operator-shaped reaches the chat: no env var, no configuration recipe, no id.
    private void assertRefused(long userId)
    {
        string text = lastText(_bot);
        Assert.Equal("🔴 This bot isn't enabled for your account.", text);
        Assert.DoesNotContain("STENCIL_", text);
        Assert.DoesNotContain(userId.ToString(), text);
        Assert.DoesNotContain("operator", text, StringComparison.OrdinalIgnoreCase);
    }

    [Theory]
    [InlineData("/url https://93.184.216.34/cat.png")]
    [InlineData("/sourcesite https://93.184.216.34/gallery")]
    [InlineData("/connect http://93.184.216.34:8090")]
    [InlineData("/blank a4 pink")]
    [InlineData("/crop x1=10% x2=90%")]
    [InlineData("/prompt make it sepia")]
    public async Task AnUnlistedUserIsRefusedForEveryCommand(string command)
    {
        UpdateRouter router = routerFor(Allowed);

        await router.HandleMessageAsync(textFrom(Stranger, command), CancellationToken.None);

        assertRefused(Stranger);
        Assert.Equal(0, _cli.EditCalls);
        Assert.Equal(0, _cli.ScrapeCalls);
        Assert.Empty(_llm.Requests);
        Assert.False((await _store.GetAsync(Stranger, CancellationToken.None)).HasImage);
    }

    // A photo is the one intake that costs disk before any command runs, so it is gated too.
    [Fact]
    public async Task AnUnlistedUserCannotUploadAPhoto()
    {
        UpdateRouter router = routerFor(Allowed);

        await router.HandleMessageAsync(photoFrom(Stranger), CancellationToken.None);

        assertRefused(Stranger);
        Assert.Empty(_bot.Requests.OfType<GetFileRequest>());
        Assert.False((await _store.GetAsync(Stranger, CancellationToken.None)).HasImage);
    }

    // A media group buffers before routing, so the gate has to run ahead of the collector.
    [Fact]
    public async Task AnUnlistedUserCannotUploadAnAlbum()
    {
        UpdateRouter router = routerFor(Allowed);
        Message member = photoFrom(Stranger);
        member.MediaGroupId = "album-1";

        await router.HandleMessageAsync(member, CancellationToken.None);

        assertRefused(Stranger);
        Assert.Empty(_bot.Requests.OfType<GetFileRequest>());
    }

    [Fact]
    public async Task AnUnlistedUsersButtonTapDoesNothing()
    {
        UpdateRouter router = routerFor(Allowed);
        Update update = new()
        {
            CallbackQuery = new CallbackQuery
            {
                Id = "q1",
                From = new User { Id = Stranger },
                Data = "bw",
                Message = new Message { Id = 2, Chat = new Chat { Id = ChatId } },
            },
        };

        await router.HandleUpdateAsync(update, CancellationToken.None);

        assertRefused(Stranger);
        Assert.Equal(0, _cli.EditCalls);
    }

    [Fact]
    public async Task AnEmptyListRefusesEveryone()
    {
        // A HashSet with no entries — NOT the tests' allow-everyone default.
        UpdateRouter router = routerFor();

        await router.HandleMessageAsync(textFrom(Allowed, "/crop x1=10%"), CancellationToken.None);

        Assert.Equal("🔴 This bot isn't accepting requests.", lastText(_bot));
        Assert.Equal(0, _cli.EditCalls);
    }

    [Theory]
    [InlineData("/start")]
    [InlineData("/help")]
    public async Task StartAndHelpStillAnswerAnUnlistedUser(string command)
    {
        UpdateRouter router = routerFor(Allowed);

        await router.HandleMessageAsync(textFrom(Stranger, command), CancellationToken.None);

        Assert.DoesNotContain("isn't enabled", lastText(_bot));
        Assert.Empty(_log.Entries);
    }

    // /start with a payload is a deep link: it connects out and fetches a project, so it is gated.
    [Fact]
    public async Task ADeepLinkedStartIsGated()
    {
        UpdateRouter router = routerFor(Allowed);

        await router.HandleMessageAsync(textFrom(Stranger, "/start c19hdHRw"), CancellationToken.None);

        assertRefused(Stranger);
    }

    [Fact]
    public async Task AListedUserIsUnaffected()
    {
        UpdateRouter router = routerFor(Allowed, Stranger);

        await router.HandleMessageAsync(textFrom(Allowed, "/blank a4 pink"), CancellationToken.None);

        Assert.True(_cli.EditCalls > 0);
        Assert.Empty(_log.Entries);
        Assert.DoesNotContain(_bot.Requests.OfType<SendMessageRequest>(), m => m.Text.Contains("isn't enabled"));
    }

    // The operator still learns how to open the gate — from the log, not the chat, once per id.
    [Fact]
    public async Task TheOperatorHintGoesToTheLogOncePerUser()
    {
        UpdateRouter router = routerFor(Allowed);

        for (int i = 0; i < 3; i++)
        {
            await router.HandleMessageAsync(textFrom(Stranger, "/crop x1=10%"), CancellationToken.None);
        }
        await router.HandleMessageAsync(textFrom(12345, "/crop x1=10%"), CancellationToken.None);

        Assert.Equal(2, _log.Entries.Count);
        Assert.All(_log.Entries, e => Assert.Equal(LogLevel.Warning, e.Level));
        Assert.Contains(_log.Messages, m => m.Contains("STENCIL_BOT_ALLOWED_USERS") && m.Contains(Stranger.ToString()));
        Assert.Contains(_log.Messages, m => m.Contains("12345"));
    }
}
