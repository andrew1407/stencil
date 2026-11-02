using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Tests.Doubles;
using Stencil.TelegramBot.Bot.Telegram.Commands;

namespace Stencil.TelegramBot.Tests.Telegram.Commands;

/// <summary><c>/format custom w h</c> takes each dim as cm, a fraction, or with a mm/in suffix; anything else is the usage reply and leaves the page alone.</summary>
public sealed class FormatCommandTests : IDisposable
{
    private const long _userId = 57;
    private const long _chatId = 68;

    private readonly string _dataDir;
    private readonly InMemorySessionStore _store = new();
    private readonly CommandHandlers _handlers;

    public FormatCommandTests()
    {
        _dataDir = TempDirs.New("bot-format");
        BotOptions options = new() { DataDir = _dataDir };
        _handlers = TestHandlers.Create(options, _store, new MockStencilCli(), new MockBotClient());
    }

    public void Dispose()
    {
        TempDirs.Delete(_dataDir);
    }

    private async Task<UserSession> formatAsync(string command)
    {
        await _handlers.DispatchAsync(_userId, _chatId, CommandParser.Parse(command), CancellationToken.None);
        return await _store.GetAsync(_userId);
    }

    [Theory]
    [InlineData("/format custom 10 15", 10.0, 15.0)]
    [InlineData("/format custom 21/2 29.7", 10.5, 29.7)]
    [InlineData("/format custom 148mm 21cm", 14.8, 21.0)]
    [InlineData("/format custom 17/2in 11in", 21.59, 27.94)]
    [InlineData("/format custom 1e1 .5", 10.0, 0.5)]
    public async Task Should_Set_Custom_Cm_Dims(string command, double widthCm, double heightCm)
    {
        UserSession session = await formatAsync(command);

        Assert.Equal("custom", session.Edits.PageFormat);
        Assert.Equal(widthCm, session.Edits.CustomPageWidth!.Value, 9);
        Assert.Equal(heightCm, session.Edits.CustomPageHeight!.Value, 9);
    }

    [Theory]
    [InlineData("/format custom 1/0 10")]
    [InlineData("/format custom 10 1/in")]
    [InlineData("/format custom 1//2 10")]
    [InlineData("/format custom 10 10px")]
    [InlineData("/format custom 0 10")]
    [InlineData("/format custom -1/2 10")]
    [InlineData("/format custom 10")]
    public async Task Should_Leave_The_Page_Alone_On_A_Malformed_Dim(string command)
    {
        UserSession session = await formatAsync(command);

        Assert.Null(session.Edits.PageFormat);
    }
}
