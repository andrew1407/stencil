using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;

namespace Stencil.TelegramBot.Tests;

/// <summary>The two scrape commands end-to-end over one rig with CLI and Telegram mocked: <c>/sourcesite</c> sends every scraped file plus a summary, <c>/sourceupload</c> isolates one still and makes it the working image. Offline — the URL is a public IP literal.</summary>
public sealed class SourceHandlerTests : IDisposable
{
    private const long _userId = 42;
    private const long _chatId = 99;
    private const string _publicUrl = "https://93.184.216.34/gallery";

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly MockBotClient _bot = new();
    private readonly InMemorySessionStore _store = new();
    private readonly CommandHandlers _handlers;

    public SourceHandlerTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-source-" + Guid.NewGuid().ToString("N"));
        _handlers = TestHandlers.Create(new BotOptions { DataDir = _dataDir }, _store, _cli, _bot);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private Task dispatch(string text) =>
        _handlers.DispatchAsync(_userId, _chatId, CommandParser.Parse(text), CancellationToken.None);

    // ── shared by both commands ──

    [Theory]
    [InlineData("/sourcesite")]
    [InlineData("/sourceupload")]
    public async Task Should_Send_The_Usage_Hint_And_Never_Scrape_Without_Args(string command)
    {
        await dispatch(command);

        SendMessageRequest usage = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains($"Usage: {command}", usage.Text);
        Assert.Equal(0, _cli.ScrapeCalls);
    }

    [Theory]
    [InlineData("/sourcesite")]
    [InlineData("/sourceupload")]
    public async Task Should_Reply_With_The_Usage_Hint_And_Not_Scrape_On_A_Bad_Option(string command)
    {
        await dispatch($"{command} {_publicUrl} minw=wide");

        SendMessageRequest reply = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("minw", reply.Text);
        Assert.Contains($"Usage: {command}", reply.Text);
        Assert.Equal(0, _cli.ScrapeCalls);
    }

    // ── /sourcesite ──

    [Fact]
    public async Task Should_Send_Each_Scraped_File_And_A_Summary_On_Site()
    {
        await dispatch($"/sourcesite {_publicUrl}");

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
    public async Task Should_Pass_Parsed_Filters_And_A_Service_Owned_Output_Dir_To_The_Cli_On_Site()
    {
        await dispatch($"/sourcesite {_publicUrl} 6 filter=img format=png|jpg name=cat.*\\.jpg minw=200 group=1");

        ScrapeRequest req = lastScrape();
        Assert.Equal(_publicUrl, req.Url);
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
    public async Task Should_Default_To_Five_On_Site_Without_A_Count()
    {
        await dispatch($"/sourcesite {_publicUrl}");

        Assert.Equal(5, lastScrape().Count);
    }

    [Fact]
    public async Task Should_Pass_Count_Zero_Through_As_All_On_Site()
    {
        // An explicit 0 means "all" — it is NOT re-defaulted to 5, and passes straight through
        // (the CLI reads `--source-count 0` as every match).
        await dispatch($"/sourcesite {_publicUrl} 0");

        Assert.Equal(0, lastScrape().Count);
    }

    // ── /sourceupload ──

    [Fact]
    public async Task Should_Load_The_Scraped_Still_As_The_Working_Image_And_Send_A_Photo_On_Upload()
    {
        await dispatch($"/sourceupload {_publicUrl}");

        // The scrape isolates exactly one still: image-category only, Count=1, Group=index(0).
        ScrapeRequest req = lastScrape();
        Assert.Equal(_publicUrl, req.Url);
        Assert.Equal("img|background|poster", req.Filter);
        Assert.Equal(1, req.Count);
        Assert.Equal(0, req.Group);

        // The session now carries an editable working image, labelled from the URL, and remembers
        // the scraped page as its source (shown in /status + the caption).
        UserSession session = await _store.GetAsync(_userId, CancellationToken.None);
        Assert.True(session.HasImage);
        Assert.Equal("gallery", session.ImageLabel);
        Assert.Equal(_publicUrl, session.SourceUrl);

        // …and the rendered result went out as a photo with the edit menu.
        Assert.Single(_bot.Requests.OfType<SendPhotoRequest>());
    }

    [Fact]
    public async Task Should_Pass_Index_And_Bound_Options_Into_The_Scrape_Request_On_Upload()
    {
        await dispatch($"/sourceupload {_publicUrl} index=0 format=png minw=200 maxh=1000");

        ScrapeRequest req = lastScrape();
        Assert.Equal(0, req.Group);       // index → Group
        Assert.Equal(1, req.Count);       // always isolate one
        Assert.Equal("png", req.Format);
        Assert.Equal(200, req.MinWidth);
        Assert.Equal(1000, req.MaxHeight);
        Assert.Equal("img|background|poster", req.Filter);
    }

    [Fact]
    public async Task Should_Reply_With_The_No_Image_Hint_And_Send_No_Photo_On_Upload_With_An_Out_Of_Range_Index()
    {
        // Only two stubs exist, so index 999 isolates nothing — the handler replies, not renders.
        await dispatch($"/sourceupload {_publicUrl} 999");

        // The last message is the "no image" reply (the first is the interim "Scraping…" notice,
        // which the mock records as a SendMessage but never deletes — its Message return is null).
        SendMessageRequest reply = _bot.Requests.OfType<SendMessageRequest>().Last();
        Assert.Contains("No image at index 999", reply.Text);
        Assert.Contains("Usage: /sourceupload", reply.Text);
        Assert.Empty(_bot.Requests.OfType<SendPhotoRequest>());

        UserSession session = await _store.GetAsync(_userId, CancellationToken.None);
        Assert.False(session.HasImage);
    }

    private ScrapeRequest lastScrape()
    {
        Assert.NotNull(_cli.LastScrapeRequest);
        return _cli.LastScrapeRequest!;
    }
}
