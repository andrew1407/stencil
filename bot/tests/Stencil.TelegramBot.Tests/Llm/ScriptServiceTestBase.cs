using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Stencil.TelegramBot.Domain.Llm.Wire;

namespace Stencil.TelegramBot.Tests.Llm;

/// <summary>The shared rig for the <see cref="ScriptService"/> suites: a real workspace, editing service and <see cref="PromptService"/> over the mock CLI, so a lowered plan takes the same route a model plan does.</summary>
public abstract class ScriptServiceTestBase : IDisposable
{
    protected const long UserId = 7;

    protected readonly string _root;
    protected readonly MockStencilCli _cli = new();
    protected readonly InMemorySessionStore _store = new();
    protected readonly UserWorkspace _workspace;
    protected readonly EditingService _editing;
    protected readonly ScriptService _service;

    protected ScriptServiceTestBase()
    {
        _root = Path.Combine(Path.GetTempPath(), "stencil-script-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _root };
        _workspace = new UserWorkspace(options);
        _editing = new EditingService(_cli, _workspace, _store);
        _service = new ScriptService(_cli, _workspace, _store, _editing, new PromptService(
            new MockLlmClient(), _editing, _store, new LlmOptions(), new MockServerClientFactory()));
    }

    public void Dispose()
    {
        try { Directory.Delete(_root, recursive: true); } catch { /* best effort */ }
        GC.SuppressFinalize(this);
    }

    protected static ScriptPlan Plan(string sourceKind, string source, params string[] actionArrays) =>
        new([], [new ScriptBlock(0, source, sourceKind, actionArrays)]);

    protected Task Adopt() => _editing.BlankAsync(UserId, new BlankSpec());
}
