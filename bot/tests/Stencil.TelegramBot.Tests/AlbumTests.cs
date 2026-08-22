using Microsoft.Extensions.Logging.Abstractions;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Telegram media-group (album) handling through the real <see cref="UpdateRouter"/>: members
/// buffer in the <see cref="AlbumCollector"/> until the settle window (driven by a test-held
/// completion source — no sleeping), then the caption runs once per photo in album order and
/// the results come back as ONE media group. Captionless members are never echoed one by one.
/// </summary>
public sealed class AlbumTests : IDisposable
{
    private const long UserId = 71;
    private const long ChatId = 72;

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly MockBotClient _bot = new();
    private readonly MockLlmClient _llm = new();
    private readonly InMemorySessionStore _store = new();
    private readonly TaskCompletionSource _settle = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly AlbumCollector _albums;
    private readonly CommandHandlers _handlers;
    private readonly UpdateRouter _router;

    public AlbumTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-album-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir };
        EditingService editing = new(_cli, new UserWorkspace(options), _store);
        _handlers = TestHandlers.Create(options, _store, _cli, _bot, _llm, editing: editing);
        _albums = new AlbumCollector(ct => _settle.Task.WaitAsync(ct));
        _router = new UpdateRouter(
            _handlers,
            new CallbackAction(_handlers, _bot, _store),
            editing,
            _store,
            _bot,
            new UserGate(),
            options,
            NullLogger<UpdateRouter>.Instance,
            _albums);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    /// <summary>Route one album member exactly as the poller would deliver it.</summary>
    private Task SendAlbumPhoto(int messageId, string fileId, string? caption = null, string group = "album-1") =>
        _router.HandleMessageAsync(
            new Message
            {
                Id = messageId,
                Chat = new Chat { Id = ChatId },
                From = new User { Id = UserId },
                Photo = [new PhotoSize { FileId = fileId, FileUniqueId = fileId, Width = 90, Height = 90 }],
                MediaGroupId = group,
                Caption = caption,
            },
            CancellationToken.None);

    private Task Send(string text) =>
        _router.HandleMessageAsync(
            new Message
            {
                Chat = new Chat { Id = ChatId },
                From = new User { Id = UserId },
                Text = text,
            },
            CancellationToken.None);

    /// <summary>Release the settle window and wait for the buffered group to flush.</summary>
    private async Task SettleAsync()
    {
        _settle.TrySetResult();
        await _albums.WhenIdleAsync();
    }

    private IEnumerable<string> DownloadedFileIds => _bot.Requests.OfType<GetFileRequest>().Select(r => r.FileId);

    [Fact]
    public async Task CaptionOnTheFirstMemberRunsThePerPhotoBatchInOrderAndRepliesWithOneAlbum()
    {
        await SendAlbumPhoto(1, "p1", caption: "/filter bw");
        await SendAlbumPhoto(2, "p2");
        await SendAlbumPhoto(3, "p3");
        await SettleAsync();

        // Each photo was downloaded and rendered once, in album order.
        Assert.Equal(["p1", "p2", "p3"], DownloadedFileIds);
        Assert.Equal(3, _cli.EditCalls);
        Assert.Equal("bw", _cli.LastRequest!.Filter);
        // One media group, in order — never per-photo echoes.
        SendMediaGroupRequest album = Assert.Single(_bot.Requests.OfType<SendMediaGroupRequest>());
        List<string> captions = album.Media.Cast<InputMedia>().Select(m => m.Caption!).ToList();
        // The lead caption — the only one a collapsed album shows — says how many results are in it.
        Assert.StartsWith("3 results\nphoto 1/3 — ", captions[0]);
        Assert.Equal(["photo 2/3", "photo 3/3"], captions.Skip(1).Select(c => c[..9]));
        Assert.Empty(_bot.Requests.OfType<SendPhotoRequest>());
        // The working image ends as the LAST photo's edited result.
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("photo 3/3", session.ImageLabel);
        Assert.Equal("bw", session.Edits.Filter);
    }

    [Fact]
    public async Task CaptionOnTheLastMemberBatchesTheSameWay()
    {
        await SendAlbumPhoto(1, "p1");
        await SendAlbumPhoto(2, "p2");
        await SendAlbumPhoto(3, "p3", caption: "/rotate 1");
        await SettleAsync();

        Assert.Equal(["p1", "p2", "p3"], DownloadedFileIds);
        Assert.Equal(3, _cli.EditCalls);
        Assert.Equal(1, _cli.LastRequest!.Rotate);
        SendMediaGroupRequest album = Assert.Single(_bot.Requests.OfType<SendMediaGroupRequest>());
        Assert.Equal(3, album.Media.Count());
        Assert.Empty(_bot.Requests.OfType<SendPhotoRequest>());
    }

    [Fact]
    public async Task ChatModeAlbumCaptionGoesToTheAssistantOncePerPhotoInOrder()
    {
        await Send("/chat");
        for (int i = 0; i < 3; i++)
        {
            _llm.CannedReplies.Enqueue(new LlmReply(
                """{"reply":"Done.","actions":[{"op":"filter","mode":"bw"}]}"""));
        }

        await SendAlbumPhoto(1, "p1", caption: "make these black and white");
        await SendAlbumPhoto(2, "p2");
        await SendAlbumPhoto(3, "p3");
        await SettleAsync();

        // One assistant turn per photo, each carrying the shared caption, in album order.
        Assert.Equal(3, _llm.Requests.Count);
        Assert.All(_llm.Requests, r => Assert.Equal("make these black and white", r.Messages[^1].Text));
        Assert.Equal(["p1", "p2", "p3"], DownloadedFileIds);
        // The edited results come back as ONE media group; the last edit owns the session.
        SendMediaGroupRequest album = Assert.Single(_bot.Requests.OfType<SendMediaGroupRequest>());
        Assert.Equal(3, album.Media.Count());
        Assert.Empty(_bot.Requests.OfType<SendPhotoRequest>());
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("photo 3/3", session.ImageLabel);
        Assert.Equal("bw", session.Edits.Filter);
    }

    [Fact]
    public async Task AnUncaptionedAlbumAdoptsOnlyTheLastPhotoWithOneNote()
    {
        await SendAlbumPhoto(1, "p1");
        await SendAlbumPhoto(2, "p2");
        await SendAlbumPhoto(3, "p3");
        await SettleAsync();

        // Only the last photo was downloaded/adopted — no per-photo echo spam.
        Assert.Equal(["p3"], DownloadedFileIds);
        SendMessageRequest note = Assert.Single(_bot.Requests.OfType<SendMessageRequest>());
        Assert.Contains("only one can be the working image", note.Text);
        Assert.Single(_bot.Requests.OfType<SendPhotoRequest>());
        Assert.Empty(_bot.Requests.OfType<SendMediaGroupRequest>());
        Assert.True((await _store.GetAsync(UserId)).HasImage);
    }

    [Fact]
    public async Task APlainCaptionOnASingleNonAlbumPhotoGoesToTheAssistantInChatMode()
    {
        await Send("/chat");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"reply":"Sepia it is.","actions":[{"op":"filter","mode":"sepia"}]}"""));

        await _router.HandleMessageAsync(
            new Message
            {
                Id = 9,
                Chat = new Chat { Id = ChatId },
                From = new User { Id = UserId },
                Photo = [new PhotoSize { FileId = "solo", FileUniqueId = "solo", Width = 90, Height = 90 }],
                Caption = "make it sepia",
            },
            CancellationToken.None);

        LlmChatRequest turn = Assert.Single(_llm.Requests);
        Assert.Equal("make it sepia", turn.Messages[^1].Text);
        Assert.Equal("sepia", (await _store.GetAsync(UserId)).Edits.Filter);
        Assert.Single(_bot.Requests.OfType<SendPhotoRequest>());
    }
}
