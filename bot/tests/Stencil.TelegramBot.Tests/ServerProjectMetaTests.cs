using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>A project's own fields through <c>ServerService</c>: colour, name, description and
/// the version-guarded expiry (including the retry past a concurrent bump).</summary>
public sealed class ServerProjectMetaTests : ServerServiceTestBase
{
    [Fact]
    public async Task SetProjectColorUpdatesTheActiveProject()
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
    public async Task SetProjectColorThrowsWithoutAnActiveProject()
    {
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await Assert.ThrowsAsync<InvalidOperationException>(() => _service.SetProjectColorAsync(UserId, "#fff"));
    }

    [Fact]
    public async Task SetProjectNameRenamesTheActiveProjectAndRelabelsTheWorkingImage()
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
    public async Task SetProjectNameRejectsABlankName()
    {
        _factory.ClientFor(ServerA).Seed(
            new ProjectRecord { Id = "p_seed", Name = "Shared", ImageW = 320, ImageH = 240, Version = 4 },
            LayoutWithFilter("none"));
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await _service.FetchAsync(UserId, "Shared", url: null);

        await Assert.ThrowsAsync<InvalidOperationException>(() => _service.SetProjectNameAsync(UserId, "   "));
    }

    [Fact]
    public async Task SetProjectNameThrowsWithoutAnActiveProject()
    {
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await Assert.ThrowsAsync<InvalidOperationException>(() => _service.SetProjectNameAsync(UserId, "New"));
    }

    [Fact]
    public async Task SetProjectDescriptionStoresItOnTheSessionForStatus()
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
    public async Task SetProjectExpirySetsAndClearsTheActiveProject()
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
    public async Task SetProjectExpiryThrowsWithoutAnActiveProject()
    {
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await Assert.ThrowsAsync<InvalidOperationException>(() => _service.SetProjectExpiryAsync(UserId, 9_000));
    }

    [Fact]
    public async Task SetProjectExpiryRetriesPastAConcurrentBump()
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
    public async Task CreateThenSetExpiryDoesNotConflict()
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
