using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The <c>/project-name</c> handler's local branch: with no active server project it just relabels
/// the working image (the name <c>/create</c> will save under) and never touches the collaboration
/// server. Runs the real <see cref="CommandHandlers"/> with a <see cref="ThrowingServerService"/>,
/// so any accidental server call fails the test loudly.
/// </summary>
public sealed class ProjectNameHandlerTests : IDisposable
{
    private const long UserId = 77;
    private const long ChatId = 88;

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly MockBotClient _bot = new();
    private readonly InMemorySessionStore _store = new();
    private readonly CommandHandlers _handlers;

    public ProjectNameHandlerTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-name-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir };
        _handlers = TestHandlers.Create(options, _store, _cli, _bot);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private Task dispatch(string text) =>
        _handlers.DispatchAsync(UserId, ChatId, CommandParser.Parse(text), CancellationToken.None);

    [Fact]
    public async Task RelabelsTheLocalWorkingImageWithoutTouchingTheServer()
    {
        await dispatch("/blank");
        await dispatch("/project-name Poster draft");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("Poster draft", session.ImageLabel);
        SendMessageRequest confirm = _bot.Requests.OfType<SendMessageRequest>().Last();
        Assert.Contains("Working image renamed to: Poster draft", confirm.Text);
    }

    [Fact]
    public async Task WithNoWorkingImageAsksForOne()
    {
        await dispatch("/project-name Whatever");

        SendMessageRequest reply = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("No working image", reply.Text);
        UserSession session = await _store.GetAsync(UserId);
        Assert.False(session.HasImage);
    }

    [Fact]
    public async Task BlankArgumentSendsUsageAndKeepsTheLabel()
    {
        await dispatch("/blank");
        UserSession before = await _store.GetAsync(UserId);

        await dispatch("/project-name    ");

        SendMessageRequest usage = _bot.Requests.OfType<SendMessageRequest>().Last();
        Assert.Contains("Usage: /project-name", usage.Text);
        UserSession after = await _store.GetAsync(UserId);
        Assert.Equal(before.ImageLabel, after.ImageLabel); // unchanged ("blank")
    }

    [Fact]
    public async Task DescriptionIsHeldLocallyAndShownInStatusBeforeSaving()
    {
        await dispatch("/blank");
        await dispatch("/project-description A red study");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("A red study", session.ActiveProjectDescription);
        Assert.Contains("description: A red study", Replies.StatusText(session));
    }

    [Fact]
    public async Task EmptyDescriptionClearsTheLocalDescription()
    {
        await dispatch("/blank");
        await dispatch("/project-description Something");
        await dispatch("/project-description");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("", session.ActiveProjectDescription);
        Assert.DoesNotContain("description:", Replies.StatusText(session));
    }
}
