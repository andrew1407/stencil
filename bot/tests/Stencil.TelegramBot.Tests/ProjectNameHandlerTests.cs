using Microsoft.Extensions.Logging.Abstractions;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Links;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Fakes;
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
    private readonly FakeStencilCli _cli = new();
    private readonly FakeBotClient _bot = new();
    private readonly InMemorySessionStore _store = new();
    private readonly CommandHandlers _handlers;

    public ProjectNameHandlerTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-name-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir };
        UserWorkspace workspace = new(options);
        EditingService editing = new(_cli, workspace, _store);
        LayoutFetcher layoutFetcher = new(options, isBlockedAddress: RemoteImageUrl.IsBlockedAddress);
        _handlers = new CommandHandlers(
            editing,
            new ThrowingServerService(),
            _store,
            _bot,
            options,
            new SyncRegistry(),
            layoutFetcher,
            NullLogger<CommandHandlers>.Instance);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private Task Dispatch(string text) =>
        _handlers.DispatchAsync(UserId, ChatId, CommandParser.Parse(text), CancellationToken.None);

    [Fact]
    public async Task RelabelsTheLocalWorkingImageWithoutTouchingTheServer()
    {
        await Dispatch("/blank");
        await Dispatch("/project-name Poster draft");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("Poster draft", session.ImageLabel);
        SendMessageRequest confirm = _bot.Requests.OfType<SendMessageRequest>().Last();
        Assert.Contains("Working image renamed to: Poster draft", confirm.Text);
    }

    [Fact]
    public async Task WithNoWorkingImageAsksForOne()
    {
        await Dispatch("/project-name Whatever");

        SendMessageRequest reply = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("No working image", reply.Text);
        UserSession session = await _store.GetAsync(UserId);
        Assert.False(session.HasImage);
    }

    [Fact]
    public async Task BlankArgumentSendsUsageAndKeepsTheLabel()
    {
        await Dispatch("/blank");
        UserSession before = await _store.GetAsync(UserId);

        await Dispatch("/project-name    ");

        SendMessageRequest usage = _bot.Requests.OfType<SendMessageRequest>().Last();
        Assert.Contains("Usage: /project-name", usage.Text);
        UserSession after = await _store.GetAsync(UserId);
        Assert.Equal(before.ImageLabel, after.ImageLabel); // unchanged ("blank")
    }

    [Fact]
    public async Task DescriptionIsHeldLocallyAndShownInStatusBeforeSaving()
    {
        await Dispatch("/blank");
        await Dispatch("/project-description A red study");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("A red study", session.ActiveProjectDescription);
        Assert.Contains("description: A red study", Replies.StatusText(session));
    }

    [Fact]
    public async Task EmptyDescriptionClearsTheLocalDescription()
    {
        await Dispatch("/blank");
        await Dispatch("/project-description Something");
        await Dispatch("/project-description");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("", session.ActiveProjectDescription);
        Assert.DoesNotContain("description:", Replies.StatusText(session));
    }
}
