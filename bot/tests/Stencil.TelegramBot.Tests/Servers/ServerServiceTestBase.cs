using System.Text.Json;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests.Servers;

/// <summary>The shared rig for the <see cref="ServerService"/> suites: a <see cref="MockServerClientFactory"/> of in-memory servers and a real <see cref="EditingService"/> over one session store.</summary>
public abstract class ServerServiceTestBase : IDisposable
{
    protected const long UserId = 555;
    protected const string ServerA = "http://a:8090";
    protected const string ServerB = "http://b:8090";

    protected readonly string _root;
    protected readonly InMemorySessionStore _store;
    protected readonly MockServerClientFactory _factory;
    protected readonly MockStencilCli _cli = new();
    protected readonly EditingService _editing;
    protected readonly ServerService _service;

    protected ServerServiceTestBase()
    {
        _root = TempDirs.New("server");
        BotOptions options = new() { DataDir = _root };
        UserWorkspace workspace = new(options);
        _store = new InMemorySessionStore();
        _editing = new EditingService(_cli, workspace, _store);
        _factory = new MockServerClientFactory();
        _service = new ServerService(_factory, _store, _editing, _cli);
    }

    public void Dispose()
    {
        TempDirs.Delete(_root);
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
