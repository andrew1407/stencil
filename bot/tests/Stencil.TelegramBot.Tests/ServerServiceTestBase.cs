using System.Text.Json;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The shared rig for the <see cref="ServerService"/> suites: a
/// <see cref="MockServerClientFactory"/> (in-memory servers) and a real
/// <see cref="EditingService"/>/<see cref="MockStencilCli"/> over one session store.
/// </summary>
public abstract class ServerServiceTestBase : IDisposable
{
    protected const long UserId = 555;
    protected const string ServerA = "http://a:8090";
    protected const string ServerB = "http://b:8090";

    protected readonly string _root;
    protected readonly InMemorySessionStore _store;
    protected readonly MockServerClientFactory _factory;
    protected readonly ServerService _service;

    protected ServerServiceTestBase()
    {
        _root = Path.Combine(Path.GetTempPath(), "stencil-server-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _root };
        UserWorkspace workspace = new(options);
        _store = new InMemorySessionStore();
        EditingService editing = new(new MockStencilCli(), workspace, _store);
        _factory = new MockServerClientFactory();
        _service = new ServerService(_factory, _store, editing);
    }

    public void Dispose()
    {
        if (Directory.Exists(_root))
        {
            Directory.Delete(_root, recursive: true);
        }
        GC.SuppressFinalize(this);
    }

    protected static JsonElement LayoutWithFilter(string filter) =>
        JsonDocument.Parse($"{{\"imageFilter\":\"{filter}\",\"lines\":[]}}").RootElement.Clone();

    protected async Task SeedWorkingImageAsync()
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
