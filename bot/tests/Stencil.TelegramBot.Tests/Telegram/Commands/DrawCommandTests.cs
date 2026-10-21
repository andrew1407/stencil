using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Layout;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Stencil.TelegramBot.Bot.Telegram.Commands;

namespace Stencil.TelegramBot.Tests.Telegram.Commands;

/// <summary><c>/draw</c> percentages measure the picture as the chat shows it — after its crop and quarter turns — the frame the stored lines are drawn in, as a script's or a plan's are.</summary>
public sealed class DrawCommandTests : IDisposable
{
    private const long _userId = 55;
    private const long _chatId = 66;

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly MockBotClient _bot = new();
    private readonly InMemorySessionStore _store = new();
    private readonly CommandHandlers _handlers;

    public DrawCommandTests()
    {
        _dataDir = TempDirs.New("bot-draw");
        BotOptions options = new() { DataDir = _dataDir };
        EditingService editing = new(_cli, new UserWorkspace(options), _store);
        _handlers = TestHandlers.Create(options, _store, _cli, _bot, editing: editing);
    }

    public void Dispose()
    {
        TempDirs.Delete(_dataDir);
    }

    private Task seedAsync(int width, int height, EditState edits) =>
        _store.SaveAsync(new UserSession
        {
            UserId = _userId,
            OriginalImagePath = Path.Combine(_dataDir, "orig.png"),
            OriginalWidth = width,
            OriginalHeight = height,
            Edits = edits,
        });

    private async Task<IReadOnlyList<LayoutPoint>> drawnAsync(string command)
    {
        await _handlers.DispatchAsync(_userId, _chatId, CommandParser.Parse(command), CancellationToken.None);
        UserSession after = await _store.GetAsync(_userId);
        return after.Edits.Layout!.Lines[^1].Points;
    }

    [Fact]
    public async Task Should_Measure_Percentages_Against_The_Cropped_Picture()
    {
        await seedAsync(500, 500, new EditState { CropSpec = "x1=10% x2=90% y1=10% y2=90%" });

        IReadOnlyList<LayoutPoint> points = await drawnAsync("/draw rect 20%,20% 80%,80%");

        // The crop leaves 400x400, so 20%..80% is 80..320 — never 100..400 of the 500x500 original;
        // a closed shape is stored back to its first corner.
        Assert.Equal(
            [new LayoutPoint(80, 80), new LayoutPoint(320, 80), new LayoutPoint(320, 320), new LayoutPoint(80, 320), new LayoutPoint(80, 80)],
            points);
    }

    [Fact]
    public async Task Should_Measure_Percentages_Against_The_Turned_Picture()
    {
        await seedAsync(600, 300, new EditState { Rotate = 1 });

        IReadOnlyList<LayoutPoint> points = await drawnAsync("/draw line 50%,50% 100%,100%");

        // A quarter turn shows the 600x300 original as 300x600.
        Assert.Equal([new LayoutPoint(150, 300), new LayoutPoint(300, 600)], points);
    }

    [Fact]
    public async Task Should_Keep_Pixel_Points_As_Written()
    {
        await seedAsync(500, 500, new EditState { CropSpec = "x1=10% x2=90% y1=10% y2=90%" });

        IReadOnlyList<LayoutPoint> points = await drawnAsync("/draw line 10,20 30,40");

        Assert.Equal([new LayoutPoint(10, 20), new LayoutPoint(30, 40)], points);
    }
}
