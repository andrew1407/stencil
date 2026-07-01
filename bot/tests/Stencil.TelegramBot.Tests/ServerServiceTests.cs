using System.Text.Json;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Fakes;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// <see cref="ServerService"/> over a <see cref="FakeServerClientFactory"/> (in-memory servers)
/// and a real <see cref="EditingService"/>/<see cref="FakeStencilCli"/> sharing one session
/// store: connect, cross-server listing, fetch (with layout-filter seeding), create+upload, and
/// version-guarded save (incl. the conflict surface).
/// </summary>
public sealed class ServerServiceTests : IDisposable
{
    private const long UserId = 555;
    private const string ServerA = "http://a:8090";
    private const string ServerB = "http://b:8090";

    private readonly string _root;
    private readonly InMemorySessionStore _store;
    private readonly FakeServerClientFactory _factory;
    private readonly ServerService _service;

    public ServerServiceTests()
    {
        _root = Path.Combine(Path.GetTempPath(), "stencil-server-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _root };
        UserWorkspace workspace = new(options);
        _store = new InMemorySessionStore();
        EditingService editing = new(new FakeStencilCli(), workspace, _store);
        _factory = new FakeServerClientFactory();
        _service = new ServerService(_factory, _store, editing);
    }

    public void Dispose()
    {
        if (Directory.Exists(_root))
        {
            Directory.Delete(_root, recursive: true);
        }
    }

    private static JsonElement LayoutWithFilter(string filter) =>
        JsonDocument.Parse($"{{\"imageFilter\":\"{filter}\",\"lines\":[]}}").RootElement.Clone();

    [Fact]
    public async Task ConnectStoresConnectionWithMintedToken()
    {
        _factory.ClientFor(ServerA).MintedToken = "minted-xyz";

        ServerConnectionInfo info = await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);

        Assert.Equal("http://a:8090", info.Url);
        Assert.Equal("minted-xyz", info.Token);
        UserSession session = await _store.GetAsync(UserId);
        ServerConnectionInfo stored = Assert.Single(session.Connections);
        Assert.Equal("minted-xyz", stored.Token);
    }

    [Fact]
    public async Task ListProjectsAggregatesAndSkipsAThrowingServer()
    {
        _factory.ClientFor(ServerA).Seed(new ProjectRecord { Id = "p_a", Name = "Alpha" });
        _factory.ClientFor(ServerB).ThrowOnList = true;
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await _service.ConnectAsync(UserId, ServerB, token: null, verifyTls: true);

        IReadOnlyList<ServerProjectInfo> projects = await _service.ListProjectsAsync(UserId, url: null);

        ServerProjectInfo only = Assert.Single(projects);
        Assert.Equal("p_a", only.Record.Id);
        Assert.Equal("http://a:8090", only.ServerUrl);
    }

    [Fact]
    public async Task FetchSetsActiveProjectAndSeedsFilterFromLayout()
    {
        _factory.ClientFor(ServerA).Seed(
            new ProjectRecord { Id = "p_seed", Name = "Shared", ImageW = 320, ImageH = 240, Version = 4 },
            LayoutWithFilter("sepia"));
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);

        UserSession session = await _service.FetchAsync(UserId, "Shared", url: null);

        Assert.Equal("p_seed", session.ActiveProjectId);
        Assert.Equal("Shared", session.ActiveProjectName);
        Assert.Equal("http://a:8090", session.ActiveServerUrl);
        Assert.Equal(4, session.ActiveProjectVersion);
        Assert.Equal("sepia", session.Edits.Filter);
        Assert.True(session.HasImage);
        Assert.True(File.Exists(session.OriginalImagePath));
    }

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
    public async Task FetchThrowsWhenNoMatch()
    {
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        await Assert.ThrowsAsync<InvalidOperationException>(
            () => _service.FetchAsync(UserId, "Nope", url: null));
    }

    [Fact]
    public async Task CreateProjectCreatesUploadsOriginalAndMarksActive()
    {
        await SeedWorkingImageAsync();
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);

        ProjectRecord record = await _service.CreateProjectAsync(UserId, "My Project", url: null);

        Assert.Equal("My Project", record.Name);
        FakeStencilServerClient client = _factory.ClientFor(ServerA);
        (string Id, string Kind, byte[] Data, string Ext, int W, int H) put = Assert.Single(client.Puts);
        Assert.Equal(record.Id, put.Id);
        Assert.Equal(ProjectFileKind.Original, put.Kind);

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(record.Id, session.ActiveProjectId);
        Assert.Equal("http://a:8090", session.ActiveServerUrl);
    }

    [Fact]
    public async Task SaveActiveProjectUpdatesUnderTheVersionGuard()
    {
        await SeedWorkingImageAsync();
        await _service.ConnectAsync(UserId, ServerA, token: null, verifyTls: true);
        ProjectRecord created = await _service.CreateProjectAsync(UserId, "Doc", url: null);

        ProjectRecord saved = await _service.SaveActiveProjectAsync(UserId);

        Assert.Equal(created.Version + 1, saved.Version);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(saved.Version, session.ActiveProjectVersion);
        FakeStencilServerClient client = _factory.ClientFor(ServerA);
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

    private async Task SeedWorkingImageAsync()
    {
        UserSession session = new()
        {
            UserId = UserId,
            OriginalImagePath = Path.Combine(_root, "orig.png"),
            OriginalWidth = 100,
            OriginalHeight = 80,
            ImageLabel = "photo",
        };
        Directory.CreateDirectory(_root);
        await File.WriteAllBytesAsync(session.OriginalImagePath, new byte[] { 1, 2, 3 });
        await _store.SaveAsync(session);
    }
}
