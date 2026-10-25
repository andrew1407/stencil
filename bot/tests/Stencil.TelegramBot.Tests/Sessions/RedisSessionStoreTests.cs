using StackExchange.Redis;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests.Sessions;

/// <summary>The Redis session store over an in-process fake: the in-memory store's contract, one JSON
/// value per user, and a stored value it cannot read starting the user afresh instead of failing every update.</summary>
public sealed class RedisSessionStoreTests
{
    private readonly RedisSessionStore _store;
    private readonly Dictionary<string, string> _values;

    public RedisSessionStoreTests()
    {
        (IConnectionMultiplexer redis, Dictionary<string, string> values) = FakeRedis.Create();
        _store = new RedisSessionStore(redis);
        _values = values;
    }

    [Fact]
    public async Task Should_Return_A_Fresh_Session_On_Get_For_An_Unknown_User()
    {
        UserSession session = await _store.GetAsync(42);
        Assert.Equal(42, session.UserId);
        Assert.False(session.HasImage);
        Assert.True(session.Edits.IsEmpty);
    }

    [Fact]
    public async Task Should_Round_Trip_On_Save_Then_Get_Under_The_Users_Key()
    {
        await _store.SaveAsync(new UserSession
        {
            UserId = 7,
            OriginalImagePath = "/tmp/img.png",
            OriginalWidth = 100,
            Edits = new EditState { Rotate = 2, Filter = "bw" },
            SaveChats = true,
        });
        Assert.Equal(["stencilbot:session:7"], _values.Keys);
        UserSession loaded = await _store.GetAsync(7);
        Assert.Equal("/tmp/img.png", loaded.OriginalImagePath);
        Assert.Equal(100, loaded.OriginalWidth);
        Assert.Equal(2, loaded.Edits.Rotate);
        Assert.Equal("bw", loaded.Edits.Filter);
        Assert.True(loaded.SaveChats);
    }

    [Fact]
    public async Task Should_Clear_The_Stored_Session_On_Reset()
    {
        await _store.SaveAsync(new UserSession { UserId = 9, OriginalImagePath = "/tmp/x.png" });
        await _store.ResetAsync(9);
        Assert.Empty(_values);
        Assert.Null((await _store.GetAsync(9)).OriginalImagePath);
    }

    [Theory]
    [InlineData("{not json")]
    [InlineData("[1, 2]")]
    [InlineData("{\"originalImagePath\": \"/tmp/old.png\"}")]
    [InlineData("{\"userId\": 8, \"originalImagePath\": \"/tmp/other.png\"}")]
    public async Task Should_Start_Afresh_When_The_Stored_Value_Cannot_Be_Read(string stored)
    {
        _values["stencilbot:session:5"] = stored;
        UserSession session = await _store.GetAsync(5);
        Assert.Equal(5, session.UserId);
        Assert.Null(session.OriginalImagePath);
        await _store.SaveAsync(session);
        Assert.Equal(5, (await _store.GetAsync(5)).UserId);
    }
}
