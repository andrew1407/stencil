using Microsoft.Extensions.Logging.Abstractions;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Links;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Fakes;
using Telegram.Bot.Requests;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The <c>/sourcesite</c> handler end-to-end through the real <see cref="CommandHandlers"/> +
/// <see cref="EditingService"/>, but with the CLI and Telegram faked: <see cref="FakeStencilCli"/>
/// materialises a directory of stub files (an image + a video) and <see cref="FakeBotClient"/>
/// captures what the handler sends. Stays fully offline — the URL uses a public IP literal so the
/// SSRF pre-check needs no DNS.
/// </summary>
public sealed class SourceSiteHandlerTests : IDisposable
{
    private const long UserId = 42;
    private const long ChatId = 99;

    // A public IP-literal host: RemoteImageUrl.ValidateAsync resolves it without any DNS lookup.
    private const string PublicUrl = "https://93.184.216.34/gallery";

    private readonly string _dataDir;
    private readonly FakeStencilCli _cli = new();
    private readonly FakeBotClient _bot = new();
    private readonly CommandHandlers _handlers;

    public SourceSiteHandlerTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-scrape-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir };
        UserWorkspace workspace = new(options);
        InMemorySessionStore store = new();
        EditingService editing = new(_cli, workspace, store);
        LayoutFetcher layoutFetcher = new(options, isBlockedAddress: RemoteImageUrl.IsBlockedAddress);
        _handlers = new CommandHandlers(
            editing,
            new ThrowingServerService(),
            store,
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
        await Dispatch("/sourcesite");

        SendMessageRequest usage = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("Usage: /sourcesite", usage.Text);
        Assert.Equal(0, _cli.ScrapeCalls);
    }

    [Fact]
    public async Task SendsEachScrapedFileAndASummary()
    {
        await Dispatch($"/sourcesite {PublicUrl}");

        // The image stub goes out as a photo (with its measured dimensions in the caption)…
        SendPhotoRequest photo = Assert.Single(_bot.Requests.OfType<SendPhotoRequest>());
        Assert.Contains("logo.png", photo.Caption);
        Assert.Contains("200x80", photo.Caption);

        // …and the video stub as a document.
        SendDocumentRequest document = Assert.Single(_bot.Requests.OfType<SendDocumentRequest>());
        Assert.Contains("clip.mp4", document.Caption);

        // The final message is the summary (count + host).
        SendMessageRequest summary = _bot.Requests.OfType<SendMessageRequest>().Last();
        Assert.Contains("Scraped 2 file(s) from 93.184.216.34", summary.Text);
    }

    [Fact]
    public async Task PassesParsedFiltersAndAServiceOwnedOutputDirToTheCli()
    {
        await Dispatch($"/sourcesite {PublicUrl} 6 filter=img format=png|jpg name=cat.*\\.jpg minw=200 group=1");

        Assert.NotNull(_cli.LastScrapeRequest);
        ScrapeRequest req = _cli.LastScrapeRequest!;
        Assert.Equal(PublicUrl, req.Url);
        Assert.Equal(6, req.Count);
        Assert.Equal(1, req.Group);
        Assert.Equal("img", req.Filter);
        Assert.Equal("png|jpg", req.Format);
        Assert.Equal("cat.*\\.jpg", req.Name);
        Assert.Equal(200, req.MinWidth);
        // The Application layer fills the output dir with a per-user scratch path under DataDir.
        Assert.StartsWith(_dataDir, req.OutputDir);
        Assert.NotEqual(_dataDir, req.OutputDir);
    }

    [Fact]
    public async Task NoCountDefaultsToFive()
    {
        await Dispatch($"/sourcesite {PublicUrl}");

        Assert.NotNull(_cli.LastScrapeRequest);
        Assert.Equal(5, _cli.LastScrapeRequest!.Count);
    }

    [Fact]
    public async Task CountZeroRidesThroughAsAll()
    {
        // An explicit 0 means "all" — it is NOT re-defaulted to 5, and passes straight through
        // (the CLI reads `--source-count 0` as every match).
        await Dispatch($"/sourcesite {PublicUrl} 0");

        Assert.NotNull(_cli.LastScrapeRequest);
        Assert.Equal(0, _cli.LastScrapeRequest!.Count);
    }

    [Fact]
    public async Task ABadOptionRepliesWithTheUsageHintAndDoesNotScrape()
    {
        await Dispatch($"/sourcesite {PublicUrl} minw=wide");

        SendMessageRequest reply = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("minw", reply.Text);
        Assert.Contains("Usage: /sourcesite", reply.Text);
        Assert.Equal(0, _cli.ScrapeCalls);
    }
}
