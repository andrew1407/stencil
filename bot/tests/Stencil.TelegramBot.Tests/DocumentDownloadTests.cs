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
/// The non-image download cap. A layout <c>.json</c> and a <c>.stencil</c> project are parsed
/// whole, so they stream to disk under <see cref="BotOptions.MaxDocumentBytes"/> instead of
/// riding the 50 MB photo limit into a <c>byte[]</c>. A photo keeps the larger cap.
/// </summary>
public sealed class DocumentDownloadTests : IDisposable
{
    private const long _userId = 55;
    private const long _chatId = 66;
    private const int _megabyte = 1024 * 1024;

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly SizedBotClient _bot = new();
    private readonly InMemorySessionStore _store = new();

    public DocumentDownloadTests() =>
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-download-" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    [Fact]
    public void TheDocumentCapIsFarTighterThanThePhotoCap()
    {
        BotOptions options = new();

        Assert.Equal(4L * _megabyte, options.MaxDocumentBytes);
        Assert.True(options.MaxDocumentBytes < options.MaxDownloadBytes);
    }

    // A smaller operator limit still wins — the document cap only ever tightens.
    [Fact]
    public void ASmallerConfiguredLimitStillWins()
    {
        BotOptions options = new() { MaxDownloadBytes = _megabyte };

        Assert.Equal(_megabyte, options.MaxDocumentBytes);
    }

    [Fact]
    public async Task AnOversizedLayoutJsonIsRefused()
    {
        _bot.FileBytes = 5 * _megabyte;
        UpdateRouter router = makeRouter();

        await router.HandleMessageAsync(documentFrom("layout.json", "/apply"), CancellationToken.None);

        Assert.Contains("too large", lastText());
        Assert.Contains("4 MB", lastText()); // the document cap, not the 50 MB photo one
        Assert.Equal(0, (await _store.GetAsync(_userId, CancellationToken.None)).Edits.LineCount);
    }

    [Fact]
    public async Task AnOversizedProjectFileIsRefused()
    {
        _bot.FileBytes = 5 * _megabyte;
        UpdateRouter router = makeRouter();

        await router.HandleMessageAsync(documentFrom("board.stencil", null), CancellationToken.None);

        Assert.Contains("too large", lastText());
        Assert.False((await _store.GetAsync(_userId, CancellationToken.None)).HasImage);
    }

    // The cap streams to disk, so the refused download leaves no partial file behind either.
    [Fact]
    public async Task AnOversizedDocumentLeavesNoTempFile()
    {
        _bot.FileBytes = 5 * _megabyte;
        UpdateRouter router = makeRouter();
        HashSet<string> before = [.. Directory.GetFiles(Path.GetTempPath(), "stencil-bot-*.json")];

        await router.HandleMessageAsync(documentFrom("layout.json", "/apply"), CancellationToken.None);

        Assert.Empty(Directory.GetFiles(Path.GetTempPath(), "stencil-bot-*.json").Except(before));
    }

    [Fact]
    public async Task ASmallLayoutJsonStillApplies()
    {
        _bot.Payload = """{"imageWidth":640,"imageHeight":480,"lines":[{"points":[{"x":1,"y":2},{"x":3,"y":4}]}]}"""u8.ToArray();
        UpdateRouter router = makeRouter();
        await adopt();

        await router.HandleMessageAsync(documentFrom("layout.json", "/apply"), CancellationToken.None);

        Assert.DoesNotContain(_bot.Requests.OfType<SendMessageRequest>(), m => m.Text.Contains("too large"));
        Assert.True((await _store.GetAsync(_userId, CancellationToken.None)).Edits.LineCount > 0);
    }

    // Photos are untouched by the tighter cap: one well past it still becomes the working image.
    [Fact]
    public async Task APhotoPastTheDocumentCapStillLoads()
    {
        _bot.FileBytes = 5 * _megabyte;
        UpdateRouter router = makeRouter();

        await router.HandleMessageAsync(photoMessage(), CancellationToken.None);

        Assert.DoesNotContain(_bot.Requests.OfType<SendMessageRequest>(), m => m.Text.Contains("too large"));
        Assert.True((await _store.GetAsync(_userId, CancellationToken.None)).HasImage);
    }

    private UpdateRouter makeRouter()
    {
        BotOptions options = new() { DataDir = _dataDir, AllowedUsers = new HashSet<long> { _userId } };
        EditingService editing = new(_cli, new UserWorkspace(options), _store);
        CommandHandlers handlers = TestHandlers.Create(options, _store, _cli, _bot, editing: editing);
        return new UpdateRouter(
            handlers, new CallbackAction(handlers, _bot, _store), editing, _store, _bot,
            new UserGate(), options, new MockLogger<UpdateRouter>());
    }

    /// <summary>Give the session a working image, so a layout upload has something to draw on.</summary>
    private async Task adopt()
    {
        long size = _bot.FileBytes;
        _bot.FileBytes = 8;
        await makeRouter().HandleMessageAsync(photoMessage(), CancellationToken.None);
        _bot.FileBytes = size;
        _bot.Requests.Clear();
    }

    private static Message documentFrom(string fileName, string? caption) =>
        new()
        {
            Id = 1,
            Chat = new Chat { Id = _chatId },
            From = new User { Id = _userId },
            Caption = caption,
            Document = new Document { FileId = "d1", FileUniqueId = "d1", FileName = fileName },
        };

    private static Message photoMessage() =>
        new()
        {
            Id = 2,
            Chat = new Chat { Id = _chatId },
            From = new User { Id = _userId },
            Photo = [new PhotoSize { FileId = "p1", FileUniqueId = "p1", Width = 90, Height = 90 }],
        };

    private string lastText() => _bot.Requests.OfType<SendMessageRequest>().Last().Text;

    /// <summary>A bot client whose downloads actually produce bytes, so the caps are exercised.</summary>
    private sealed class SizedBotClient : MockBotClient
    {
        /// <summary>How many bytes a download writes, when <see cref="Payload"/> is unset.</summary>
        public long FileBytes { get; set; } = 8;

        /// <summary>Exact bytes to hand back instead of filler.</summary>
        public byte[]? Payload { get; set; }

        public override async Task DownloadFile(string filePath, Stream destination, CancellationToken cancellationToken = default)
        {
            if (Payload is byte[] payload)
            {
                await destination.WriteAsync(payload, cancellationToken);
                return;
            }
            byte[] chunk = new byte[64 * 1024];
            for (long written = 0; written < FileBytes; written += chunk.Length)
            {
                int count = (int)Math.Min(chunk.Length, FileBytes - written);
                await destination.WriteAsync(chunk.AsMemory(0, count), cancellationToken);
            }
        }
    }
}
