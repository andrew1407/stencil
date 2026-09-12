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

/// <summary>
/// The context suffix the turn carries: connections and the active project, never a token — plus
/// the multi-image and URL-normalisation cases that read it.
/// </summary>
public sealed class PromptContextTests : PromptServiceTestBase
{
    public PromptContextTests(PromptServiceFixture fixture) : base(fixture) { }

    [Fact]
    public async Task Should_List_Connection_Urls_And_The_Active_Project_But_Never_A_Token_In_The_Context_Suffix()
    {
        await SeedImage();
        await SeedConnections(Saved("http://alpha:8090", "secret-alpha"), Saved("https://beta:9090", "secret-beta"));
        await SeedActiveProject("portrait");
        Reply("chat");
        await Prompt("what am I connected to?");

        string system = _llm.Requests[^1].System!;
        Assert.Contains("http://alpha:8090", system);
        Assert.Contains("https://beta:9090", system);
        Assert.Contains("\"portrait\" on http://localhost:8090", system);
        // Tokens are redacted entirely — URLs only.
        Assert.DoesNotContain("secret-alpha", system);
        Assert.DoesNotContain("secret-beta", system);
    }

    [Fact]
    public async Task Should_Say_So_In_The_Context_Suffix_When_There_Are_No_Connections()
    {
        await SeedImage();
        Reply("chat");
        await Prompt("am I connected to anything?");

        string system = _llm.Requests[^1].System!;
        Assert.Contains("no collaboration-server connections", system);
        Assert.DoesNotContain("Active server project", system);
    }

    [Fact]
    public async Task Should_Run_Silently_In_One_Round_For_A_Multi_Image_Layout_Plan()
    {
        await SeedImage();
        PromptService service = WithAttachments();
        Reply(
            """
            {"reply":"outlined","actions":[
              {"op":"image","index":1},
              {"op":"layout","lines":[{"points":[{"x":1,"y":2},{"x":3,"y":4},{"x":1,"y":2}]}]}]}
            """);

        PromptOutcome outcome = await service.PromptAsync(UserId, "outline each of them", null, CancellationToken.None);

        // The only model call is the plan itself, and no skipped-pass note is appended.
        Assert.Single(_llm.Requests);
        Assert.Empty(outcome.Warnings);
    }

    [Fact]
    public async Task Should_Normalize_The_Configured_Server_Url_Like_The_Connections_It_Matches()
    {
        UserSession session = await _store.GetAsync(UserId);
        await _store.SaveAsync(session with
        {
            // Stored as /connect stores it: the factory-normalised origin.
            Connections = [new ServerConnectionInfo { Url = "http://localhost:8090", Token = "local-tok" }],
        });
        PromptService service = new(_llm, _editing, _store, new LlmOptions
        {
            Provider = LlmOptions.PROVIDER_STENCIL_SERVER,
            // A bare host, as an env var would plausibly carry it.
            ServerUrl = "localhost:8090",
        }, new MockServerClientFactory());
        _llm.CannedReplies.Enqueue(new LlmReply("hello"));

        await service.PromptAsync(UserId, "hi", null, CancellationToken.None);

        LlmChatRequest request = _llm.Requests[^1];
        Assert.Equal("http://localhost:8090", request.ServerUrl);
        Assert.Equal("local-tok", request.ServerToken);
    }

    // ── §2 undo / redo / reset (the /undo /redo /reset paths) ──
}
