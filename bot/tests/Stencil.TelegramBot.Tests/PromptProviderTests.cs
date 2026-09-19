using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>Plans that need no image, and the §6.3 stencil-server provider resolving which server (and whose token) a turn is proxied through.</summary>
public sealed class PromptProviderTests : PromptServiceTestBase
{
    public PromptProviderTests(PromptServiceFixture fixture) : base(fixture) { }

    [Fact]
    public async Task Should_Run_A_Blank_Action_Without_A_Working_Image()
    {
        int baseline = _cli.EditCalls;
        Reply("""{"reply":"fresh page","actions":[{"op":"blank","color":"#ffffff","format":"a4"}]}""");

        PromptOutcome outcome = await Prompt("start a blank a4");

        Assert.True(outcome.Mutated); // the caller renders + sends the fresh page
        Assert.Empty(outcome.Renders);
        Assert.Equal(baseline + 1, _cli.EditCalls); // the blank itself; the main render is the caller's
        UserSession session = await _store.GetAsync(UserId);
        Assert.True(session.HasImage);
        Assert.Equal("a4", session.Edits.PageFormat);
    }

    [Fact]
    public async Task Should_Reject_A_Non_Blank_Plan_Before_Executing_Without_A_Working_Image()
    {
        int baseline = _cli.EditCalls;
        Reply("""{"reply":"ok","actions":[{"op":"filter","mode":"bw"}]}""");

        PromptOutcome outcome = await Prompt("bw please");

        Assert.Contains("no working image", outcome.Reply, StringComparison.OrdinalIgnoreCase);
        Assert.Empty(outcome.Renders);
        Assert.Equal(baseline, _cli.EditCalls);
    }

    [Fact]
    public async Task Should_Ride_The_Edit_State_For_A_Formula_Action()
    {
        await SeedImage();
        Reply("""{"reply":"formula set","actions":[{"op":"formula","axis":"x","expr":"x*2+10"}]}""");

        await Prompt("double x");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("x*2+10", session.Edits.FormulaX);
        Assert.Null(session.Edits.FormulaY);
    }

    [Fact]
    public async Task Should_Resolve_The_Users_First_Connection_For_The_Stencil_Server_Provider()
    {
        InMemorySessionStore store = new();
        BotOptions options = new() { DataDir = _dataDir };
        EditingService editing = new(_cli, new UserWorkspace(options), store);
        PromptService service = new(_llm, editing, store,
            new LlmOptions { Provider = LlmOptions.PROVIDER_STENCIL_SERVER },
            new MockServerClientFactory());
        UserSession session = await store.GetAsync(UserId);
        await store.SaveAsync(session with
        {
            Connections = [new ServerConnectionInfo { Url = "http://h:8090", Token = "tok-1" }],
        });
        _llm.CannedReplies.Enqueue(new LlmReply("hello"));

        await service.PromptAsync(UserId, "hi", null, CancellationToken.None);

        LlmChatRequest request = _llm.Requests[^1];
        Assert.Equal("http://h:8090", request.ServerUrl);
        Assert.Equal("tok-1", request.ServerToken);
    }

    [Fact]
    public async Task Should_Ask_To_Connect_For_The_Stencil_Server_Provider_Without_Any_Connection()
    {
        PromptService service = new(_llm, _editing, _store,
            new LlmOptions { Provider = LlmOptions.PROVIDER_STENCIL_SERVER },
            new MockServerClientFactory());

        InvalidOperationException ex = await Assert.ThrowsAsync<InvalidOperationException>(() =>
            service.PromptAsync(UserId, "hi", null, CancellationToken.None));
        Assert.Contains("/connect", ex.Message);
    }

    [Fact]
    public async Task Should_Let_An_Explicit_Server_Url_Win_And_Reuse_A_Matching_Connections_Token()
    {
        UserSession session = await _store.GetAsync(UserId);
        await _store.SaveAsync(session with
        {
            Connections =
            [
                new ServerConnectionInfo { Url = "http://other:8090", Token = "other-tok" },
                new ServerConnectionInfo { Url = "http://mine:8090", Token = "mine-tok" },
            ],
        });
        PromptService service = new(_llm, _editing, _store, new LlmOptions
        {
            Provider = LlmOptions.PROVIDER_STENCIL_SERVER,
            ServerUrl = "http://mine:8090/",
        }, new MockServerClientFactory());
        _llm.CannedReplies.Enqueue(new LlmReply("hello"));

        await service.PromptAsync(UserId, "hi", null, CancellationToken.None);

        LlmChatRequest request = _llm.Requests[^1];
        Assert.Equal("http://mine:8090", request.ServerUrl);
        Assert.Equal("mine-tok", request.ServerToken);
    }

    [Fact]
    public async Task Should_Authenticate_A_User_Who_Never_Connected_With_The_Configured_Server_Token()
    {
        PromptService service = new(_llm, _editing, _store, new LlmOptions
        {
            Provider = LlmOptions.PROVIDER_STENCIL_SERVER,
            ServerUrl = "http://proxy:8090",
            ServerToken = "operator-tok",
        }, new MockServerClientFactory());
        _llm.CannedReplies.Enqueue(new LlmReply("hello"));

        await service.PromptAsync(UserId, "hi", null, CancellationToken.None);

        LlmChatRequest request = _llm.Requests[^1];
        Assert.Equal("http://proxy:8090", request.ServerUrl);
        Assert.Equal("operator-tok", request.ServerToken);
    }

    [Fact]
    public async Task Should_Let_A_Users_Own_Connection_Token_Win_Over_The_Configured_One()
    {
        UserSession session = await _store.GetAsync(UserId);
        await _store.SaveAsync(session with
        {
            Connections = [new ServerConnectionInfo { Url = "http://proxy:8090", Token = "mine-tok" }],
        });
        PromptService service = new(_llm, _editing, _store, new LlmOptions
        {
            Provider = LlmOptions.PROVIDER_STENCIL_SERVER,
            ServerUrl = "http://proxy:8090",
            ServerToken = "operator-tok",
        }, new MockServerClientFactory());
        _llm.CannedReplies.Enqueue(new LlmReply("hello"));

        await service.PromptAsync(UserId, "hi", null, CancellationToken.None);

        Assert.Equal("mine-tok", _llm.Requests[^1].ServerToken);
    }

    [Fact]
    public async Task Should_Tell_The_User_To_Connect_Instead_Of_Returning_A_401_With_No_Token_At_All()
    {
        PromptService service = new(_llm, _editing, _store, new LlmOptions
        {
            Provider = LlmOptions.PROVIDER_STENCIL_SERVER,
            ServerUrl = "http://proxy:8090",
        }, new MockServerClientFactory());

        InvalidOperationException ex = await Assert.ThrowsAsync<InvalidOperationException>(() =>
            service.PromptAsync(UserId, "hi", null, CancellationToken.None));

        // Names the server and the command — never an empty bearer the server answers 401 to.
        Assert.Contains("/connect http://proxy:8090", ex.Message);
        Assert.Empty(_llm.Requests);
    }
}
