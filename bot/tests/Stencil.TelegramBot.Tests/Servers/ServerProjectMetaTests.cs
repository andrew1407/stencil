using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Tests.Servers;

/// <summary>A project's own fields through <c>ServerService</c>: colour, name, description and
/// the version-guarded expiry (including the retry past a concurrent bump).</summary>
public sealed class ServerProjectMetaTests : ServerServiceTestBase
{
    [Fact]
    public async Task Should_Update_The_Active_Project_On_Set_Project_Color()
    {
        _factory.ClientFor(ServerA).Seed(
            new ProjectRecord { Id = "p_seed", Name = "Shared", ImageW = 320, ImageH = 240, Version = 4 },
            LayoutWithFilter("none"));
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await _service.FetchAsync(UserId, "Shared", url: null);

        string color = await _service.SetProjectColorAsync(UserId, "#ff8800");
        Assert.Equal("#ff8800", color);
    }

    [Fact]
    public async Task Should_Throw_Without_An_Active_Project_On_Set_Project_Color()
    {
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await Assert.ThrowsAsync<InvalidOperationException>(() => _service.SetProjectColorAsync(UserId, "#fff"));
    }

    [Fact]
    public async Task Should_Rename_The_Active_Project_And_Relabel_The_Working_Image_On_Set_Project_Name()
    {
        _factory.ClientFor(ServerA).Seed(
            new ProjectRecord { Id = "p_seed", Name = "Shared", ImageW = 320, ImageH = 240, Version = 4 },
            LayoutWithFilter("none"));
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await _service.FetchAsync(UserId, "Shared", url: null);

        string name = await _service.SetProjectNameAsync(UserId, "  Poster draft  ");
        Assert.Equal("Poster draft", name); // trimmed
        UserSession after = await _store.GetAsync(UserId);
        Assert.Equal("Poster draft", after.ActiveProjectName);
        Assert.Equal("Poster draft", after.ImageLabel); // the working-image label follows the name
        Assert.Equal(5, after.ActiveProjectVersion); // bumped from the fetched v4
    }

    [Fact]
    public async Task Should_Reject_A_Blank_Name_On_Set_Project_Name()
    {
        _factory.ClientFor(ServerA).Seed(
            new ProjectRecord { Id = "p_seed", Name = "Shared", ImageW = 320, ImageH = 240, Version = 4 },
            LayoutWithFilter("none"));
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await _service.FetchAsync(UserId, "Shared", url: null);

        await Assert.ThrowsAsync<InvalidOperationException>(() => _service.SetProjectNameAsync(UserId, "   "));
    }

    [Fact]
    public async Task Should_Throw_Without_An_Active_Project_On_Set_Project_Name()
    {
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await Assert.ThrowsAsync<InvalidOperationException>(() => _service.SetProjectNameAsync(UserId, "New"));
    }

    [Fact]
    public async Task Should_Store_It_On_The_Session_For_Status_On_Set_Project_Description()
    {
        _factory.ClientFor(ServerA).Seed(
            new ProjectRecord { Id = "p_seed", Name = "Shared", ImageW = 320, ImageH = 240, Version = 4 },
            LayoutWithFilter("none"));
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await _service.FetchAsync(UserId, "Shared", url: null);

        string desc = await _service.SetProjectDescriptionAsync(UserId, "A test poster");
        Assert.Equal("A test poster", desc);
        UserSession afterSet = await _store.GetAsync(UserId);
        Assert.Equal("A test poster", afterSet.ActiveProjectDescription);

        // An empty argument clears it back off the session.
        await _service.SetProjectDescriptionAsync(UserId, "");
        UserSession afterClear = await _store.GetAsync(UserId);
        Assert.Equal("", afterClear.ActiveProjectDescription);
    }

    [Fact]
    public async Task Should_Set_And_Clear_The_Active_Project_On_Set_Project_Expiry()
    {
        _factory.ClientFor(ServerA).Seed(
            new ProjectRecord { Id = "p_seed", Name = "Shared", ImageW = 320, ImageH = 240, Version = 4 },
            LayoutWithFilter("none"));
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await _service.FetchAsync(UserId, "Shared", url: null);

        long set = await _service.SetProjectExpiryAsync(UserId, 9_000);
        Assert.Equal(9_000, set);
        UserSession afterSet = await _store.GetAsync(UserId);
        Assert.Equal(9_000, afterSet.ActiveProjectExpiresAt);
        Assert.Equal(5, afterSet.ActiveProjectVersion); // bumped from the fetched v4

        // 0 clears the expiry (keep forever), and the version guard advances again.
        long cleared = await _service.SetProjectExpiryAsync(UserId, 0);
        Assert.Equal(0, cleared);
        UserSession afterClear = await _store.GetAsync(UserId);
        Assert.Equal(0, afterClear.ActiveProjectExpiresAt);
    }

    [Fact]
    public async Task Should_Throw_Without_An_Active_Project_On_Set_Project_Expiry()
    {
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await Assert.ThrowsAsync<InvalidOperationException>(() => _service.SetProjectExpiryAsync(UserId, 9_000));
    }

    [Fact]
    public async Task Should_Retry_Past_A_Concurrent_Bump_On_Set_Project_Expiry()
    {
        _factory.ClientFor(ServerA).Seed(
            new ProjectRecord { Id = "p_seed", Name = "Shared", ImageW = 320, ImageH = 240, Version = 4 },
            LayoutWithFilter("none"));
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await _service.FetchAsync(UserId, "Shared", url: null);
        // Another writer bumps the server version, so our stored version is now stale — the field
        // write must re-read the current version and still land, not surface a conflict.
        _factory.ClientFor(ServerA).BumpVersion("p_seed");

        long set = await _service.SetProjectExpiryAsync(UserId, 9_000);
        Assert.Equal(9_000, set);
    }

    [Fact]
    public async Task Should_Not_Conflict_On_Create_Then_Set_Expiry()
    {
        // Regression: the original upload bumps the server version, so a naive create that stored
        // the pre-upload version would 409 on this immediately-following expiry write.
        await SeedWorkingImageAsync();
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await _service.CreateProjectAsync(UserId, "Doc", url: null);

        long set = await _service.SetProjectExpiryAsync(UserId, 12_000);
        Assert.Equal(12_000, set);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(12_000, session.ActiveProjectExpiresAt);
    }

}
