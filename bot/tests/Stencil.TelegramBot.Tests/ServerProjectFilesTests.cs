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
    public async Task Should_Create_Upload_Original_And_Mark_Active_On_Create_Project()
    {
        await SeedWorkingImageAsync();
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);

        ProjectRecord record = await _service.CreateProjectAsync(UserId, "My Project", url: null);

        Assert.Equal("My Project", record.Name);
        MockStencilServerClient client = _factory.ClientFor(ServerA);
        (string Id, string Kind, byte[] Data, string Ext, int W, int H) put = Assert.Single(client.Puts);
        Assert.Equal(record.Id, put.Id);
        Assert.Equal(ProjectFileKind.ORIGINAL, put.Kind);

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(record.Id, session.ActiveProjectId);
        Assert.Equal("http://a:8090", session.ActiveServerUrl);
    }

    [Fact]
    public async Task Should_Upload_A_Locally_Held_Description_On_Create_Project()
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
    public async Task Should_Update_Under_The_Version_Guard_On_Save_Active_Project()
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
        Assert.Contains(client.Puts, p => p.Kind == ProjectFileKind.RESULT);
    }

    [Fact]
    public async Task Should_Surface_A_Conflict_On_Save_Active_Project()
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
    public async Task Should_Remove_It_And_Clear_The_Session_On_Delete_Active_Project()
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
    public async Task Should_Throw_Without_An_Active_Project_On_Delete_Active_Project()
    {
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await Assert.ThrowsAsync<InvalidOperationException>(() => _service.DeleteActiveProjectAsync(UserId));
    }

}
