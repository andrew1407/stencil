using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;
using Stencil.TelegramBot.Domain.Llm.Wire;

namespace Stencil.TelegramBot.Tests.Llm.Prompt;

/// <summary>One temp workspace per prompt-test class: the rig is cheap, the data directory is not, so it is created and deleted once per class rather than per <c>[Fact]</c>.</summary>
public sealed class PromptServiceFixture : IDisposable
{
    public string DataDir { get; } =
        Path.Combine(Path.GetTempPath(), "stencil-bot-prompt-" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        try { Directory.Delete(DataDir, recursive: true); } catch { /* best effort */ }
    }
}

/// <summary>The prompt engine's shared rig: the real <see cref="EditingService"/> with the LLM and CLI mocked. Every test gets its OWN store, mocks and service — only the temp directory is shared — so the bands stay order-independent.</summary>
public abstract class PromptServiceTestBase : IClassFixture<PromptServiceFixture>
{
    protected const long UserId = 7;

    protected readonly string _dataDir;
    protected readonly MockStencilCli _cli = new();
    protected readonly MockLlmClient _llm = new();
    protected readonly InMemorySessionStore _store = new();
    protected readonly EditingService _editing;
    protected readonly PromptService _service;

    protected PromptServiceTestBase(PromptServiceFixture fixture)
    {
        _dataDir = fixture.DataDir;
        BotOptions options = new() { DataDir = _dataDir };
        _editing = new EditingService(_cli, new UserWorkspace(options), _store);
        _service = new PromptService(_llm, _editing, _store, new LlmOptions(), new MockServerClientFactory());
    }

    protected Task<PromptOutcome> Prompt(string text, LlmImage? image = null) =>
        _service.PromptAsync(UserId, text, image, CancellationToken.None);

    protected void Reply(string text) => _llm.CannedReplies.Enqueue(new LlmReply(text));

    /// <summary>Give the session a working image (one CLI call for the blank render).</summary>
    protected Task SeedImage() => _editing.BlankAsync(UserId, new BlankSpec(null, null, null, null));

    /// <summary>A service with the attachment loader, arming the §7 edge-map path.</summary>
    protected PromptService WithAttachments() =>
        new(_llm, _editing, _store, new LlmOptions(), new MockServerClientFactory(),
            new LlmAttachmentLoader(new MockImageDownscaler()));

    /// <summary>A PromptService whose §2.1 `save` can reach the mock collaboration server.</summary>
    protected PromptService WithProjects(RecordingServerService projects) =>
        new(_llm, _editing, _store, new LlmOptions(), new MockServerClientFactory(),
            new LlmAttachmentLoader(new MockImageDownscaler()), projects);

    /// <summary>Base64 of the stub bytes <see cref="MockStencilCli"/> writes to every output.</summary>
    protected static readonly string StubRenderBase64 = Convert.ToBase64String(new byte[] { 0x89, 0x50 });

    /// <summary>Give the session an active server project, as /create or /fetch leaves it.</summary>
    protected async Task SeedActiveProject(string name = "cat")
    {
        UserSession session = await _store.GetAsync(UserId);
        await _store.SaveAsync(session with
        {
            ActiveServerUrl = "http://localhost:8090",
            ActiveProjectId = "p1",
            ActiveProjectName = name,
        });
    }

    /// <summary>Store connections as /connect leaves them (normalised origins + stored tokens).</summary>
    protected async Task SeedConnections(params ServerConnectionInfo[] connections)
    {
        UserSession session = await _store.GetAsync(UserId);
        await _store.SaveAsync(session with { Connections = connections });
    }

    protected static ServerConnectionInfo Saved(string url, string token = "") =>
        new() { Url = url, Token = token };
}
