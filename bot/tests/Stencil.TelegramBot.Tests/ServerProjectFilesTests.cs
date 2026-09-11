using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>Creating, saving and deleting the active project through <c>ServerService</c> —
/// upload on create, the version guard on save and its conflict surface.</summary>
public sealed class ServerProjectFilesTests : ServerServiceTestBase
{
    [Fact]
    public async Task CreateProjectCreatesUploadsOriginalAndMarksActive()
    {
        await SeedWorkingImageAsync();
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);

        ProjectRecord record = await _service.CreateProjectAsync(UserId, "My Project", url: null);

        Assert.Equal("My Project", record.Name);
        MockStencilServerClient client = _factory.ClientFor(ServerA);
        (string Id, string Kind, byte[] Data, string Ext, int W, int H) put = Assert.Single(client.Puts);
        Assert.Equal(record.Id, put.Id);
        Assert.Equal(ProjectFileKind.Original, put.Kind);

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(record.Id, session.ActiveProjectId);
        Assert.Equal("http://a:8090", session.ActiveServerUrl);
    }

    [Fact]
    public async Task CreateProjectUploadsALocallyHeldDescription()
    {
        await SeedWorkingImageAsync();
        // A description set before saving (via /project-description) rides on the session.
        UserSession seeded = await _store.GetAsync(UserId);
        await _store.SaveAsync(seeded with { ActiveProjectDescription = "A red study" });
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);

        ProjectRecord record = await _service.CreateProjectAsync(UserId, "My Project", url: null);

        Assert.Equal("A red study", record.Description); // uploaded in the create request
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("A red study", session.ActiveProjectDescription); // kept on the now-server project
    }

    [Fact]
    public async Task SaveActiveProjectUpdatesUnderTheVersionGuard()
    {
        await SeedWorkingImageAsync();
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        ProjectRecord created = await _service.CreateProjectAsync(UserId, "Doc", url: null);

        ProjectRecord saved = await _service.SaveActiveProjectAsync(UserId);

        // The layout update and the result upload each bump the server version, and the service
        // re-reads it after the upload, so the saved/session version advances past the created one.
        Assert.True(saved.Version > created.Version);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(saved.Version, session.ActiveProjectVersion);
        MockStencilServerClient client = _factory.ClientFor(ServerA);
        Assert.Contains(client.Puts, p => p.Kind == ProjectFileKind.Result);
    }

    [Fact]
    public async Task SaveActiveProjectSurfacesAConflict()
    {
        await SeedWorkingImageAsync();
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        ProjectRecord created = await _service.CreateProjectAsync(UserId, "Doc", url: null);
        // Another writer bumps the server version, so our stored version is now stale.
        _factory.ClientFor(ServerA).BumpVersion(created.Id);

        ServerException ex = await Assert.ThrowsAsync<ServerException>(
            () => _service.SaveActiveProjectAsync(UserId));

        Assert.True(ex.IsConflict);
    }

    [Fact]
    public async Task DeleteActiveProjectRemovesItAndClearsTheSession()
    {
        _factory.ClientFor(ServerA).Seed(
            new ProjectRecord { Id = "p_seed", Name = "Shared", ImageW = 320, ImageH = 240, Version = 4 },
            LayoutWithFilter("none"));
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await _service.FetchAsync(UserId, "Shared", url: null);

        string removed = await _service.DeleteActiveProjectAsync(UserId);

        Assert.Equal("Shared", removed);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Null(session.ActiveProjectId);
        Assert.Null(session.ActiveServerUrl);
        Assert.False(session.SyncEnabled);
        // The project is gone from the server, so it no longer lists.
        IReadOnlyList<ServerProjectInfo> projects = await _service.ListProjectsAsync(UserId, url: null);
        Assert.DoesNotContain(projects, p => p.Record.Id == "p_seed");
    }

    [Fact]
    public async Task DeleteActiveProjectThrowsWithoutAnActiveProject()
    {
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await Assert.ThrowsAsync<InvalidOperationException>(() => _service.DeleteActiveProjectAsync(UserId));
    }

}
