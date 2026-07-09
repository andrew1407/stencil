using Microsoft.Extensions.Logging.Abstractions;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Links;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Fakes;
using Telegram.Bot.Requests;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The <c>/sourceupload</c> handler end-to-end through the real <see cref="CommandHandlers"/> +
/// <see cref="EditingService"/>, with the CLI and Telegram faked. It composes the existing
/// <see cref="IEditingService.ScrapeAsync"/> (Count=1/Group=index) + <see
/// cref="IEditingService.SetImageFromLocalFileAsync"/> + the shared render path: a scraped still
/// becomes the editable working image and comes back as a photo with the edit menu. Stays fully
/// offline — the URL uses a public IP literal so the SSRF pre-check needs no DNS.
/// </summary>
public sealed class SourceUploadHandlerTests : IDisposable
{
    private const long UserId = 42;
    private const long ChatId = 99;

    // A public IP-literal host: RemoteImageUrl.ValidateAsync resolves it without any DNS lookup.
    private const string PublicUrl = "https://93.184.216.34/gallery";

    private readonly string _dataDir;
    private readonly FakeStencilCli _cli = new();
    private readonly FakeBotClient _bot = new();
    private readonly InMemorySessionStore _store = new();
    private readonly CommandHandlers _handlers;

    public SourceUploadHandlerTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-upload-" + Guid.NewGuid().ToString("N"));
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
    public async Task NoArgsSendsUsageHintAndNeverScrapes()
    {
        await Dispatch("/sourceupload");

        SendMessageRequest usage = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("Usage: /sourceupload", usage.Text);
        Assert.Equal(0, _cli.ScrapeCalls);
    }

    [Fact]
    public async Task LoadsTheScrapedStillAsTheWorkingImageAndSendsAPhoto()
    {
        await Dispatch($"/sourceupload {PublicUrl}");

        // The scrape isolates exactly one still: image-category only, Count=1, Group=index(0).
        Assert.NotNull(_cli.LastScrapeRequest);
        ScrapeRequest req = _cli.LastScrapeRequest!;
        Assert.Equal(PublicUrl, req.Url);
        Assert.Equal("img|background|poster", req.Filter);
        Assert.Equal(1, req.Count);
        Assert.Equal(0, req.Group);

        // The session now carries an editable working image, labelled from the URL, and remembers
        // the scraped page as its source (shown in /status + the caption).
        UserSession session = await _store.GetAsync(UserId, CancellationToken.None);
        Assert.True(session.HasImage);
        Assert.Equal("gallery", session.ImageLabel);
        Assert.Equal(PublicUrl, session.SourceUrl);

        // …and the rendered result went out as a photo with the edit menu.
        Assert.Single(_bot.Requests.OfType<SendPhotoRequest>());
    }

    [Fact]
    public async Task IndexAndBoundOptionsRideIntoTheScrapeRequest()
    {
        await Dispatch($"/sourceupload {PublicUrl} index=0 format=png minw=200 maxh=1000");

        Assert.NotNull(_cli.LastScrapeRequest);
        ScrapeRequest req = _cli.LastScrapeRequest!;
        Assert.Equal(0, req.Group);       // index → Group
        Assert.Equal(1, req.Count);       // always isolate one
        Assert.Equal("png", req.Format);
        Assert.Equal(200, req.MinWidth);
        Assert.Equal(1000, req.MaxHeight);
        Assert.Equal("img|background|poster", req.Filter);
    }

    [Fact]
    public async Task OutOfRangeIndexRepliesWithTheNoImageHintAndSendsNoPhoto()
    {
        // Only two stubs exist, so index 999 isolates nothing — the handler replies, not renders.
        await Dispatch($"/sourceupload {PublicUrl} 999");

        // The last message is the "no image" reply (the first is the interim "Scraping…" notice,
        // which the fake records as a SendMessage but never deletes — its Message return is null).
        SendMessageRequest reply = _bot.Requests.OfType<SendMessageRequest>().Last();
        Assert.Contains("No image at index 999", reply.Text);
        Assert.Contains("Usage: /sourceupload", reply.Text);
        Assert.Empty(_bot.Requests.OfType<SendPhotoRequest>());

        UserSession session = await _store.GetAsync(UserId, CancellationToken.None);
        Assert.False(session.HasImage);
    }

    [Fact]
    public async Task ABadOptionRepliesWithTheUsageHintAndDoesNotScrape()
    {
        await Dispatch($"/sourceupload {PublicUrl} minw=wide");

        SendMessageRequest reply = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("minw", reply.Text);
        Assert.Contains("Usage: /sourceupload", reply.Text);
        Assert.Equal(0, _cli.ScrapeCalls);
    }
}
