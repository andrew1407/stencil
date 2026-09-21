using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests.Editing;

/// <summary>The shared rig for the <see cref="EditingService"/> suites: a <see cref="MockStencilCli"/>, a real <see cref="InMemorySessionStore"/> and a real <see cref="UserWorkspace"/> rooted at a temp directory.</summary>
public abstract class EditingServiceTestBase : IDisposable
{
    protected const long UserId = 1234;

    protected readonly string _root;
    protected readonly MockStencilCli _cli;
    protected readonly InMemorySessionStore _store;
    protected readonly EditingService _service;

    protected EditingServiceTestBase()
    {
        _root = Path.Combine(Path.GetTempPath(), "stencil-editing-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _root };
        UserWorkspace workspace = new(options);
        _cli = new MockStencilCli();
        _store = new InMemorySessionStore();
        _service = new EditingService(_cli, workspace, _store);
    }

    public void Dispose()
    {
        if (Directory.Exists(_root))
        {
            Directory.Delete(_root, recursive: true);
        }
        GC.SuppressFinalize(this);
    }
}
