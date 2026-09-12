using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Sessions;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The process-local <see cref="InMemorySessionStore"/>: fresh session for an unknown user,
/// save/get round-trip, and reset clears.
/// </summary>
public sealed class InMemorySessionStoreTests
{
    [Fact]
    public async Task Should_Return_A_Fresh_Session_On_Get_For_An_Unknown_User()
    {
        InMemorySessionStore store = new();
        UserSession session = await store.GetAsync(42);
        Assert.Equal(42, session.UserId);
        Assert.Null(session.OriginalImagePath);
        Assert.False(session.HasImage);
        Assert.True(session.Edits.IsEmpty);
    }

    [Fact]
    public async Task Should_Round_Trip_On_Save_Then_Get()
    {
        InMemorySessionStore store = new();
        UserSession session = new()
        {
            UserId = 7,
            OriginalImagePath = "/tmp/img.png",
            OriginalWidth = 100,
            OriginalHeight = 200,
            ImageLabel = "photo",
            Edits = new EditState { Rotate = 2, Filter = "bw" },
            SaveChats = true,
        };
        await store.SaveAsync(session);
        UserSession loaded = await store.GetAsync(7);
        Assert.Equal("/tmp/img.png", loaded.OriginalImagePath);
        Assert.Equal(100, loaded.OriginalWidth);
        Assert.Equal(2, loaded.Edits.Rotate);
        Assert.Equal("bw", loaded.Edits.Filter);
        Assert.True(loaded.SaveChats);
    }

    [Fact]
    public async Task Should_Clear_The_Stored_Session_On_Reset()
    {
        InMemorySessionStore store = new();
        UserSession session = new()
        {
            UserId = 9,
            OriginalImagePath = "/tmp/x.png",
        };
        await store.SaveAsync(session);
        await store.ResetAsync(9);
        UserSession after = await store.GetAsync(9);
        Assert.Null(after.OriginalImagePath);
        Assert.Equal(9, after.UserId);
    }
}
