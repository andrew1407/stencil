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
/// The prompt engine end-to-end against the real <see cref="EditingService"/>, with the LLM and
/// CLI mocked: plan actions fold into the session's <see cref="EditState"/> and flag the main
/// render for the caller's shared render-and-send path; variants render one CLI invocation
/// each from a state copy; chat-only turns never render; history is bounded to 32 with the
/// image-replay rule applied, and idle users' histories are evicted beyond the tracked bound.
/// </summary>
public sealed class PromptServiceTests : IDisposable
{
    private const long UserId = 7;

    private readonly string _dataDir;
    private readonly MockStencilCli _cli = new();
    private readonly MockLlmClient _llm = new();
    private readonly InMemorySessionStore _store = new();
    private readonly EditingService _editing;
    private readonly PromptService _service;

    public PromptServiceTests()
    {
        _dataDir = Path.Combine(Path.GetTempPath(), "stencil-bot-prompt-" + Guid.NewGuid().ToString("N"));
        BotOptions options = new() { DataDir = _dataDir };
        UserWorkspace workspace = new(options);
        _editing = new EditingService(_cli, workspace, _store);
        _service = new PromptService(_llm, _editing, _store, new LlmOptions(), new MockServerClientFactory());
    }

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private Task<PromptOutcome> Prompt(string text, LlmImage? image = null) =>
        _service.PromptAsync(UserId, text, image, CancellationToken.None);

    private void Reply(string text) => _llm.CannedReplies.Enqueue(new LlmReply(text));

    /// <summary>Give the session a working image (one CLI call for the blank render).</summary>
    private Task SeedImage() => _editing.BlankAsync(UserId, new BlankSpec(null, null, null, null));

    [Fact]
    public async Task ChatOnlyTurnSendsNoRenderAndReturnsTheRawText()
    {
        await SeedImage();
        int baseline = _cli.EditCalls;
        Reply("Nothing to edit — that image already looks great!");

        PromptOutcome outcome = await Prompt("what do you think?");

        Assert.Equal("Nothing to edit — that image already looks great!", outcome.Reply);
        Assert.Empty(outcome.Renders);
        Assert.Equal(baseline, _cli.EditCalls);
    }

    [Fact]
    public async Task ActionsFoldIntoTheSessionStateAndFlagTheMainRenderForTheCaller()
    {
        await SeedImage();
        int baseline = _cli.EditCalls;
        Reply(
            """
            {"reply":"rotated and tinted","actions":[
              {"op":"rotate","dir":"right","times":1},
              {"op":"filter","mode":"custom","tint":"#ff0000"},
              {"op":"crop","spec":{"x1":"10%","x2":"-10%"}}]}
            """);

        PromptOutcome outcome = await Prompt("rotate, tint red, crop");

        Assert.Equal("rotated and tinted", outcome.Reply);
        // The main result is NOT rendered here — Mutated tells the caller to send it through
        // the shared RenderAndSendAsync path (caption, edit menu, auto-sync).
        Assert.True(outcome.Mutated);
        Assert.Empty(outcome.Renders);
        Assert.Equal(baseline, _cli.EditCalls);
        // That render then carries the folded state — the same argv the slash commands build.
        await _editing.RenderAsync(UserId);
        EditRequest request = _cli.LastRequest!;
        Assert.Equal(1, request.Rotate);
        Assert.Equal("#ff0000", request.Filter);
        Assert.Equal("x1=10% x2=-10%", request.CropSpec);
        // …and the session was really mutated (undo works on AI edits).
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(1, session.Edits.Rotate);
        Assert.Equal("#ff0000", session.Edits.Filter);
        Assert.True(session.EditHistory.Count > 0);
    }

    [Fact]
    public async Task TwoVariantsRenderTwiceFromStateCopiesWithoutMutatingTheSession()
    {
        await SeedImage();
        int baseline = _cli.EditCalls;
        Reply(
            """
            {"reply":"two takes","actions":[],"variants":[
              {"label":"rotated","actions":[{"op":"rotate","dir":"left","times":1}]},
              {"label":"sepia","actions":[{"op":"filter","mode":"sepia"}]}]}
            """);

        PromptOutcome outcome = await Prompt("give me 2 variants");

        Assert.Equal(baseline + 2, _cli.EditCalls);
        Assert.False(outcome.Mutated); // no top-level actions — nothing for the caller to render
        Assert.Equal(2, outcome.Renders.Count);
        Assert.Equal("rotated", outcome.Renders[0].Label);
        Assert.Equal("sepia", outcome.Renders[1].Label);
        // The last CLI call is the second variant: sepia, no rotation.
        EditRequest request = _cli.LastRequest!;
        Assert.Equal("sepia", request.Filter);
        Assert.Null(request.Rotate);
        // The session's own state stays untouched by variant folding.
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(0, session.Edits.Rotate);
        Assert.Null(session.Edits.Filter);
    }

    [Fact]
    public async Task VariantsBranchFromTheStateAfterTopLevelActions()
    {
        await SeedImage();
        Reply(
            """
            {"reply":"bw + one rotated variant","actions":[{"op":"filter","mode":"bw"}],
             "variants":[{"label":"turned","actions":[{"op":"rotate","dir":"right","times":2}]}]}
            """);

        PromptOutcome outcome = await Prompt("bw, plus a rotated take");

        Assert.True(outcome.Mutated); // the caller renders + sends the main bw result
        PromptRender variant = Assert.Single(outcome.Renders);
        Assert.Equal("turned", variant.Label);
        // The variant render inherits the top-level filter AND adds its own rotation.
        EditRequest request = _cli.LastRequest!;
        Assert.Equal("bw", request.Filter);
        Assert.Equal(2, request.Rotate);
    }

    // Contract §1: a variant carrying a top-level-only / settings op costs THAT variant its
    // place — the turn's real work (the actions and the sound variant) still lands.
    [Fact]
    public async Task AMisplacedOpDropsOnlyItsOwnVariantAndTheRestOfThePlanRuns()
    {
        await SeedImage();
        Reply(
            """
            {"reply":"bw plus a take","actions":[{"op":"filter","mode":"bw"}],"variants":[
              {"label":"wiped","actions":[{"op":"clear"}]},
              {"label":"turned","actions":[{"op":"rotate","dir":"left"}]}]}
            """);

        PromptOutcome outcome = await Prompt("bw, one cleared and one rotated");

        Assert.Equal("bw plus a take", outcome.Reply);   // no "invalid plan" anywhere
        Assert.True(outcome.Mutated);                    // the top-level bw still applies
        Assert.Equal("turned", Assert.Single(outcome.Renders).Label);
        string warning = Assert.Single(outcome.Warnings);
        Assert.Contains("variant 1", warning);
        Assert.Contains("wiped", warning);
        Assert.Contains("\"clear\"", warning);
        // The dropped variant never touched the session either.
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("bw", session.Edits.Filter);
        Assert.True(session.HasImage);
    }

    [Fact]
    public async Task APlanWhoseOnlyVariantIsDroppedRepliesNormallyWithTheWarning()
    {
        await SeedImage();
        int baseline = _cli.EditCalls;
        Reply("""{"reply":"here you go","variants":[{"label":"saved","actions":[{"op":"save"}]}]}""");

        PromptOutcome outcome = await Prompt("save a copy as a variant");

        Assert.Equal("here you go", outcome.Reply);
        Assert.False(outcome.Mutated);
        Assert.Empty(outcome.Renders);
        Assert.Equal(baseline, _cli.EditCalls);
        Assert.Contains("saved", Assert.Single(outcome.Warnings));
    }

    [Fact]
    public async Task UnknownOpWarningSurvivesIntoTheOutcome()
    {
        await SeedImage();
        Reply("""{"reply":"ok","actions":[{"op":"sharpen"},{"op":"filter","mode":"bw"}]}""");

        PromptOutcome outcome = await Prompt("sharpen and bw");

        string warning = Assert.Single(outcome.Warnings);
        Assert.Contains("sharpen", warning);
        Assert.True(outcome.Mutated);
    }

    [Fact]
    public async Task InvalidPlanExecutesNothing()
    {
        await SeedImage();
        int baseline = _cli.EditCalls;
        Reply("""{"reply":"ok","actions":[{"op":"rotate","dir":"up"}]}""");

        PromptOutcome outcome = await Prompt("rotate up");

        Assert.Contains("invalid plan", outcome.Reply);
        Assert.Empty(outcome.Renders);
        Assert.Equal(baseline, _cli.EditCalls);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(0, session.Edits.Rotate);
    }

    [Fact]
    public async Task FrameOpWithoutAVideoIsAPlanLevelErrorAndNothingRuns()
    {
        await SeedImage(); // an image, but no video source
        int baseline = _cli.EditCalls;
        Reply("""{"reply":"ok","actions":[{"op":"frame","index":3}]}""");

        PromptOutcome outcome = await Prompt("grab frame 3");

        Assert.Contains("video", outcome.Reply);
        Assert.Empty(outcome.Renders);
        Assert.Equal(baseline, _cli.EditCalls);
    }

    [Fact]
    public async Task HistoryIsBoundedToTheMostRecent32Messages()
    {
        await SeedImage();
        for (int i = 0; i < 25; i++)
        {
            Reply("just chatting");
            await Prompt($"turn {i}");
        }

        // Each turn adds 2 history messages; the request replays at most 32 + the current one.
        LlmChatRequest last = _llm.Requests[^1];
        Assert.Equal(PromptService.MaxHistoryMessages + 1, last.Messages.Count);
        // The oldest turns fell off the front; the newest user message is the current turn.
        Assert.Equal("turn 24", last.Messages[^1].Text);
        Assert.Equal(LlmMessage.RoleUser, last.Messages[^1].Role);
    }

    [Fact]
    public async Task ImageReplayKeepsOnlyTheCurrentTurnsImagePlusTheMostRecentPriorOne()
    {
        await SeedImage();
        LlmImage a = new("image/png", "AAAA");
        LlmImage b = new("image/png", "BBBB");
        LlmImage c = new("image/png", "CCCC");
        Reply("chat"); await Prompt("first", a);
        Reply("chat"); await Prompt("second", b);
        Reply("chat"); await Prompt("third", c);

        LlmChatRequest last = _llm.Requests[^1];
        Assert.Equal(5, last.Messages.Count); // 4 history + current
        // Older image-bearing turns replay text-only…
        Assert.Empty(last.Messages[0].Images);                 // "first" (a stripped)
        // …only the single most recent PRIOR image survives…
        Assert.Equal("BBBB", Assert.Single(last.Messages[2].Images).Base64Data); // "second"
        // …plus the current turn's image.
        Assert.Equal("CCCC", Assert.Single(last.Messages[^1].Images).Base64Data);
    }

    [Fact]
    public async Task IdleUsersHistoriesAreEvictedBeyondTheTrackedBound()
    {
        // Fill the registry past the cap: user 0 first, then MaxTrackedUsers more.
        for (long id = 0; id <= PromptService.MaxTrackedUsers; id++)
        {
            Reply("chat");
            await _service.PromptAsync(id, "hi", null, CancellationToken.None);
        }

        // The least-recently-active user (0) was forgotten — their next turn starts fresh…
        Reply("chat");
        await _service.PromptAsync(0, "again", null, CancellationToken.None);
        Assert.Single(_llm.Requests[^1].Messages);
        // …while a recently-active user still replays their conversation (2 history + current).
        Reply("chat");
        await _service.PromptAsync(PromptService.MaxTrackedUsers, "again", null, CancellationToken.None);
        Assert.Equal(3, _llm.Requests[^1].Messages.Count);
    }

    [Fact]
    public async Task SystemPromptIsTheCanonicalConstantWithTheBotOpsBlockAndAContextSuffixAppended()
    {
        await SeedImage();
        Reply("chat");
        await Prompt("hi");

        LlmChatRequest request = _llm.Requests[^1];
        // The bot's chat prompt is §4 with ONLY the §10 bot-ops block spliced in at the op
        // list's end — the canonical constant itself stays byte-identical.
        Assert.StartsWith(PromptService.ChatSystemPrompt, request.System);
        Assert.DoesNotContain(PromptService.BotOpsPrompt, PromptService.SystemPrompt);
        Assert.Contains(PromptService.BotOpsPrompt, PromptService.ChatSystemPrompt);
        Assert.Contains("640x480", request.System); // MockStencilCli's canned size
    }

    [Fact]
    public void PromptsAreAssembledFromTheOpRegistry()
    {
        // §13: no hand-maintained ops block — §4's op list and the §10 profile block are the
        // registry's generated sections verbatim (name/flag/phrase pins live in
        // OpRegistryTests; the prose core around the ops is the embedded canonical asset).
        Assert.Contains("\n" + OpRegistry.CoreOpsSection + "\n", PromptService.SystemPrompt);
        Assert.StartsWith(OpRegistry.ProfileOpsSection, PromptService.BotOpsPrompt);
        Assert.EndsWith("These ops are not image edits and cannot appear inside \"variants\".",
            PromptService.BotOpsPrompt);
        // Every registered op is named exactly where its scope flag says it belongs.
        foreach (OpDescriptor op in OpRegistry.Ops)
        {
            string home = op.Profile ? PromptService.BotOpsPrompt : PromptService.SystemPrompt;
            string other = op.Profile ? PromptService.SystemPrompt : PromptService.BotOpsPrompt;
            Assert.Contains($"\"op\":\"{op.Names[0]}\"", home);
            Assert.DoesNotContain($"- {{\"op\":\"{op.Names[0]}\"", other);
        }
    }

    [Fact]
    public void SystemPromptCarriesTheLayoutTracingGuidance()
    {
        Assert.Contains("The attached image is the ground truth", PromptService.SystemPrompt);
        Assert.Contains("trace ONLY what the user", PromptService.SystemPrompt);
        Assert.Contains("about 8-16 for an organic shape, 4-8 for a small feature", PromptService.SystemPrompt);
        Assert.Contains("never draw a remembered template", PromptService.SystemPrompt);
        Assert.Contains("edge-map attachment, when present, shows the true edges", PromptService.SystemPrompt);
        Assert.DoesNotContain("remembered template of the thing", PromptService.SystemPrompt);
        Assert.DoesNotContain("landmark mask", PromptService.SystemPrompt);
        Assert.DoesNotContain("up to 40", PromptService.SystemPrompt);
    }

    [Fact]
    public async Task BlankActionWorksWithoutAWorkingImage()
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
    public async Task NonBlankPlanWithoutAWorkingImageIsRejectedBeforeExecuting()
    {
        int baseline = _cli.EditCalls;
        Reply("""{"reply":"ok","actions":[{"op":"filter","mode":"bw"}]}""");

        PromptOutcome outcome = await Prompt("bw please");

        Assert.Contains("no working image", outcome.Reply, StringComparison.OrdinalIgnoreCase);
        Assert.Empty(outcome.Renders);
        Assert.Equal(baseline, _cli.EditCalls);
    }

    [Fact]
    public async Task FormulaActionRidesTheEditState()
    {
        await SeedImage();
        Reply("""{"reply":"formula set","actions":[{"op":"formula","axis":"x","expr":"x*2+10"}]}""");

        await Prompt("double x");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("x*2+10", session.Edits.FormulaX);
        Assert.Null(session.Edits.FormulaY);
    }

    [Fact]
    public async Task StencilServerProviderResolvesTheUsersFirstConnection()
    {
        InMemorySessionStore store = new();
        BotOptions options = new() { DataDir = _dataDir };
        EditingService editing = new(_cli, new UserWorkspace(options), store);
        PromptService service = new(_llm, editing, store,
            new LlmOptions { Provider = LlmOptions.ProviderStencilServer },
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
    public async Task StencilServerProviderWithoutAnyConnectionAsksToConnect()
    {
        PromptService service = new(_llm, _editing, _store,
            new LlmOptions { Provider = LlmOptions.ProviderStencilServer },
            new MockServerClientFactory());

        InvalidOperationException ex = await Assert.ThrowsAsync<InvalidOperationException>(() =>
            service.PromptAsync(UserId, "hi", null, CancellationToken.None));
        Assert.Contains("/connect", ex.Message);
    }

    [Fact]
    public async Task ExplicitServerUrlWinsAndReusesAMatchingConnectionsToken()
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
            Provider = LlmOptions.ProviderStencilServer,
            ServerUrl = "http://mine:8090/",
        }, new MockServerClientFactory());
        _llm.CannedReplies.Enqueue(new LlmReply("hello"));

        await service.PromptAsync(UserId, "hi", null, CancellationToken.None);

        LlmChatRequest request = _llm.Requests[^1];
        Assert.Equal("http://mine:8090", request.ServerUrl);
        Assert.Equal("mine-tok", request.ServerToken);
    }

    [Fact]
    public async Task TheConfiguredServerTokenAuthenticatesAUserWhoNeverConnected()
    {
        PromptService service = new(_llm, _editing, _store, new LlmOptions
        {
            Provider = LlmOptions.ProviderStencilServer,
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
    public async Task AUsersOwnConnectionTokenStillWinsOverTheConfiguredOne()
    {
        UserSession session = await _store.GetAsync(UserId);
        await _store.SaveAsync(session with
        {
            Connections = [new ServerConnectionInfo { Url = "http://proxy:8090", Token = "mine-tok" }],
        });
        PromptService service = new(_llm, _editing, _store, new LlmOptions
        {
            Provider = LlmOptions.ProviderStencilServer,
            ServerUrl = "http://proxy:8090",
            ServerToken = "operator-tok",
        }, new MockServerClientFactory());
        _llm.CannedReplies.Enqueue(new LlmReply("hello"));

        await service.PromptAsync(UserId, "hi", null, CancellationToken.None);

        Assert.Equal("mine-tok", _llm.Requests[^1].ServerToken);
    }

    [Fact]
    public async Task WithNoTokenAtAllTheUserIsToldToConnectInsteadOfGettingA401()
    {
        PromptService service = new(_llm, _editing, _store, new LlmOptions
        {
            Provider = LlmOptions.ProviderStencilServer,
            ServerUrl = "http://proxy:8090",
        }, new MockServerClientFactory());

        InvalidOperationException ex = await Assert.ThrowsAsync<InvalidOperationException>(() =>
            service.PromptAsync(UserId, "hi", null, CancellationToken.None));

        // Names the server and the command — never an empty bearer the server answers 401 to.
        Assert.Contains("/connect http://proxy:8090", ex.Message);
        Assert.Empty(_llm.Requests);
    }

    /// <summary>A service with the attachment loader, arming the §7 edge-map path.</summary>
    private PromptService WithAttachments() =>
        new(_llm, _editing, _store, new LlmOptions(), new MockServerClientFactory(),
            new LlmAttachmentLoader(new MockImageDownscaler()));

    /// <summary>Base64 of the stub bytes <see cref="MockStencilCli"/> writes to every output.</summary>
    private static readonly string StubRenderBase64 = Convert.ToBase64String(new byte[] { 0x89, 0x50 });

    [Fact]
    public async Task WorkingImageTurnAttachesTheEdgeMapSecondAndTheSuffixSentence()
    {
        await SeedImage();
        PromptService service = WithAttachments();
        Reply("chat");

        await service.PromptAsync(UserId, "outline the cat", new LlmImage("image/png", "AAAA"), CancellationToken.None);

        LlmChatRequest request = _llm.Requests[^1];
        LlmMessage current = request.Messages[^1];
        // The edge map rides directly after the working snapshot…
        Assert.Equal(2, current.Images.Count);
        Assert.Equal("AAAA", current.Images[0].Base64Data);
        Assert.Equal(StubRenderBase64, current.Images[1].Base64Data);
        // …the suffix carries the §7 sentence…
        Assert.EndsWith(PromptService.EdgeMapSentence, request.System);
        // …and it was produced by the CLI's contour filter over the working image.
        UserSession session = await _store.GetAsync(UserId);
        EditRequest contour = _cli.LastRequest!;
        Assert.Equal("contour", contour.Filter);
        Assert.Equal(session.OriginalImagePath, contour.Input);
    }

    [Fact]
    public async Task TextOnlyTurnCarriesNeitherEdgeMapNorSentence()
    {
        await SeedImage();
        PromptService service = WithAttachments();
        int baseline = _cli.EditCalls;
        Reply("chat");

        await service.PromptAsync(UserId, "hi", null, CancellationToken.None);

        LlmChatRequest request = _llm.Requests[^1];
        Assert.Empty(request.Messages[^1].Images);
        Assert.DoesNotContain(PromptService.EdgeMapSentence, request.System);
        Assert.Equal(baseline, _cli.EditCalls); // no contour render was even attempted
    }

    [Fact]
    public async Task EdgeMapRenderFailureIsSilentlySkipped()
    {
        await SeedImage();
        _cli.FailWhen = r => r.Filter == "contour";
        PromptService service = WithAttachments();
        Reply("chat");

        PromptOutcome outcome = await service.PromptAsync(
            UserId, "outline", new LlmImage("image/png", "AAAA"), CancellationToken.None);

        LlmChatRequest request = _llm.Requests[^1];
        Assert.Equal("AAAA", Assert.Single(request.Messages[^1].Images).Base64Data);
        Assert.DoesNotContain(PromptService.EdgeMapSentence, request.System);
        Assert.Empty(outcome.Warnings); // never fails or complains about the turn
        Assert.Equal("chat", outcome.Reply);
    }

    [Fact]
    public async Task EdgeMapIsNeverReplayedAsThePriorTurnsImage()
    {
        await SeedImage();
        PromptService service = WithAttachments();
        Reply("chat");
        await service.PromptAsync(UserId, "first", new LlmImage("image/png", "AAAA"), CancellationToken.None);
        Reply("chat");
        await service.PromptAsync(UserId, "second", new LlmImage("image/png", "BBBB"), CancellationToken.None);

        // The replayed prior user turn carries exactly its working snapshot — not its edge map.
        LlmChatRequest last = _llm.Requests[^1];
        Assert.Equal("AAAA", Assert.Single(last.Messages[0].Images).Base64Data);
    }

    // ── §7 auto-continuation ──
    // A plan whose actions CONTAIN a load op (blank/frame — the bot has no openUrl) and drew
    // NO layout planned blind; the turn is re-sent ONCE with the fresh working image attached.

    [Fact]
    public async Task MixedLoadPlanWithoutALayoutContinuesOnceWithTheFreshImage()
    {
        PromptService service = WithAttachments();
        Reply("""{"reply":"made it","actions":[{"op":"blank","color":"#ffffff","format":"a4"},{"op":"filter","mode":"bw"}]}""");
        Reply("""{"reply":"cropped it","actions":[{"op":"crop","spec":{"x1":"10%"}}]}""");

        PromptOutcome outcome = await service.PromptAsync(
            UserId, "blank a4, b&w, then crop it", null, CancellationToken.None);

        Assert.Equal(2, _llm.Requests.Count);
        LlmMessage replay = _llm.Requests[^1].Messages[^1];
        // The restated request + the §7 note, with the freshly rendered page attached.
        Assert.StartsWith("blank a4, b&w, then crop it", replay.Text);
        Assert.Contains("[The working image is now", replay.Text);
        Assert.Equal(StubRenderBase64, replay.Images[0].Base64Data);
        // The continued round's answer stands, and both rounds' edits landed.
        Assert.Equal("cropped it", outcome.Reply);
        Assert.True(outcome.Mutated);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("bw", session.Edits.Filter);
        Assert.Equal("x1=10%", session.Edits.CropSpec);
    }

    [Fact]
    public async Task TheContinuationNoteIsReplayedButNeverPersisted()
    {
        PromptService service = WithAttachments();
        Reply("""{"reply":"made it","actions":[{"op":"blank","color":"#ffffff","format":"a4"}]}""");
        Reply("""{"reply":"cropped it","actions":[{"op":"crop","spec":{"x1":"10%"}}]}""");

        await service.PromptAsync(UserId, "blank a4, then crop it", null, CancellationToken.None);

        // The note rides the wire (§7) — but §12.1's document is shared across surfaces and
        // must read as a conversation, so it carries the user's own words only.
        Assert.Contains("[The working image is now", _llm.Requests[^1].Messages[^1].Text);
        ChatDocument doc = service.BuildChatDocument(UserId)!;
        Assert.DoesNotContain("The working image is now", doc.ToJson());
        Assert.Equal(
            ["blank a4, then crop it", "made it", "blank a4, then crop it", "cropped it"],
            doc.Messages.Select(m => m.Text));
    }

    [Fact]
    public void SeedHistoryRefusesMachineryFromADirtyDocument()
    {
        // A document from another surface (or an older build) may still carry §7 internals;
        // restoring must not put them back into the replayed conversation (§12.1).
        ChatDocument dirty = new()
        {
            Messages =
            [
                new ChatDocumentMessage("user", "[The working image is now the frame — carry on.]"),
                new ChatDocumentMessage("user", "crop it\n\n" + ChatDocument.ContinuationNote),
                new ChatDocumentMessage("assistant", """{"version":1,"reply":"Cropped.","actions":[]}"""),
                new ChatDocumentMessage("assistant", "Cropped."),
            ],
        };

        Assert.Equal(2, _service.SeedHistory(UserId, dirty));
        ChatDocument doc = _service.BuildChatDocument(UserId)!;
        Assert.Equal(["crop it", "Cropped."], doc.Messages.Select(m => m.Text));
    }

    [Fact]
    public async Task ALoadPlanThatDrewALayoutIsNotContinued()
    {
        PromptService service = WithAttachments();
        Reply(
            """
            {"reply":"made and marked","actions":[{"op":"blank","color":"#ffffff"},
              {"op":"layout","lines":[{"points":[{"x":1,"y":2}]}]}]}
            """);

        PromptOutcome outcome = await service.PromptAsync(
            UserId, "blank page with a mark", null, CancellationToken.None);

        // One round, and never a continuation one.
        Assert.Single(_llm.Requests);
        Assert.All(_llm.Requests, r => Assert.DoesNotContain("[The working image is now", r.Messages[^1].Text));
        Assert.Equal("made and marked", outcome.Reply);
    }

    [Fact]
    public async Task ContinuationIsBoundedToASingleRoundPerTurn()
    {
        PromptService service = WithAttachments();
        Reply("""{"reply":"one","actions":[{"op":"blank","color":"#ffffff"},{"op":"filter","mode":"bw"}]}""");
        // The continued round answers with ANOTHER continuable plan — it must not re-fire.
        Reply("""{"reply":"two","actions":[{"op":"blank","color":"#000000"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "blank it", null, CancellationToken.None);

        Assert.Equal(2, _llm.Requests.Count);
        Assert.Empty(_llm.CannedReplies);
        Assert.Equal("two", outcome.Reply);
    }

    private const string LayoutPlan =
        """
        {"reply":"outlined","actions":[{"op":"layout","lines":[
          {"points":[{"x":1,"y":2},{"x":3,"y":4},{"x":1,"y":2}],"color":"#112233","thickness":5}]}]}
        """;

    // ── §3.0: one model round per turn ──
    // The withdrawn §3.2 correction pass is gone: a layout-drawing turn spends exactly ONE
    // model call, the traced lines are the result, and no self-check note reaches the reply.

    [Fact]
    public async Task ALayoutTurnIssuesExactlyOneModelRound()
    {
        await SeedImage();
        PromptService service = WithAttachments();
        Reply(LayoutPlan);

        PromptOutcome outcome = await service.PromptAsync(UserId, "outline the cat", null, CancellationToken.None);

        // One round, nothing after it.
        Assert.Single(_llm.Requests);
        Assert.Empty(outcome.Warnings);
        Assert.Equal("outlined", outcome.Reply);
        Assert.True(outcome.Mutated);
        // The model's own trace stands, styling included.
        UserSession session = await _store.GetAsync(UserId);
        Domain.Layout.LayoutLine line = Assert.Single(session.Edits.Layout!.Lines);
        Assert.Equal(1, line.Points[0].X);
        Assert.Equal("#112233", line.Color);
        Assert.Equal(5, line.Thickness);
        // Only the user turn + its reply — no extra pass ever enters the conversation.
        Assert.Equal(2, service.BuildChatDocument(UserId)!.Messages.Count);
    }

    /// <summary>A plan whose two lines have DISJOINT bounding boxes — the old §3.2 suspect shape.</summary>
    private const string DisjointLayoutPlan =
        """
        {"reply":"outlined","actions":[{"op":"layout","lines":[
          {"points":[{"x":1,"y":2},{"x":3,"y":4}],"color":"#112233"},
          {"points":[{"x":100,"y":100},{"x":120,"y":120}],"color":"#445566"}]}]}
        """;

    [Fact]
    public async Task AStrayLineNoLongerTriggersAnyFollowUpRound()
    {
        await SeedImage();
        PromptService service = WithAttachments();
        Reply(DisjointLayoutPlan);

        PromptOutcome outcome = await service.PromptAsync(UserId, "outline both", null, CancellationToken.None);

        Assert.Single(_llm.Requests);
        Assert.Empty(outcome.Warnings);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(2, session.Edits.Layout!.Lines.Count);
        Assert.Equal(100, session.Edits.Layout.Lines[1].Points[0].X);
    }

    [Fact]
    public async Task NoSelfCheckOrCorrectionNoteEverReachesTheReply()
    {
        await SeedImage();
        PromptService service = WithAttachments();
        Reply(LayoutPlan);

        PromptOutcome outcome = await service.PromptAsync(UserId, "outline the cat", null, CancellationToken.None);

        string shown = outcome.Reply + string.Join("\n", outcome.Warnings);
        foreach (string withdrawn in new[]
                 { "self-check", "correction", "kept the first trace", "keeping the lines", "sharpen" })
        {
            Assert.DoesNotContain(withdrawn, shown, StringComparison.OrdinalIgnoreCase);
        }
    }

    [Fact]
    public async Task ALayoutTurnRendersNothingExtraForAWithdrawnPass()
    {
        await SeedImage();
        PromptService service = WithAttachments();
        int baseline = _cli.EditCalls;
        Reply(LayoutPlan);

        // A text-only turn: no §7 attachment, so the only CLI work is the layout apply itself —
        // never a rendered-with-lines shot or a line-less contour for a follow-up round.
        await service.PromptAsync(UserId, "outline the cat", null, CancellationToken.None);

        Assert.Equal(baseline, _cli.EditCalls);
        Assert.Single(_llm.Requests);
    }

    [Fact]
    public async Task CropBeforeLayoutRemapsThePointsIntoTheCroppedFrame()
    {
        await SeedImage();   // 640x480
        Reply(
            """
            {"reply":"ok","actions":[
              {"op":"crop","spec":{"x1":"100px","y1":"50px"}},
              {"op":"layout","lines":[{"points":[{"x":150,"y":60},{"x":90,"y":40}]}]}]}
            """);

        await Prompt("crop then outline");

        // Crop resolves to rect (100,50,540,430); layout points shift by its origin and clamp.
        UserSession session = await _store.GetAsync(UserId);
        Domain.Layout.LayoutLine line = Assert.Single(session.Edits.Layout!.Lines);
        Assert.Equal(new Domain.Layout.LayoutPoint(50, 10), line.Points[0]);
        Assert.Equal(new Domain.Layout.LayoutPoint(0, 0), line.Points[1]);   // (-10,-10) clamped
        Assert.Equal("x1=100px y1=50px", session.Edits.CropSpec);            // the spec still stores as sent
    }

    [Fact]
    public async Task RotateBeforeLayoutRemapsThePointsThroughTheRotation()
    {
        await SeedImage();   // 640x480
        Reply(
            """
            {"reply":"ok","actions":[
              {"op":"rotate","dir":"right","times":1},
              {"op":"layout","lines":[{"points":[{"x":100,"y":40},{"x":0,"y":0}]}]}]}
            """);

        await Prompt("rotate then outline");

        // One CW quarter on 640x480: (x, y) -> (480 - y, x).
        UserSession session = await _store.GetAsync(UserId);
        Domain.Layout.LayoutLine line = Assert.Single(session.Edits.Layout!.Lines);
        Assert.Equal(new Domain.Layout.LayoutPoint(440, 100), line.Points[0]);
        Assert.Equal(new Domain.Layout.LayoutPoint(480, 0), line.Points[1]);
    }

    [Fact]
    public async Task LayoutOnlyPlanStillClampsOutOfBoundsPoints()
    {
        await SeedImage();   // 640x480
        Reply(
            """
            {"reply":"ok","actions":[{"op":"layout","lines":[
              {"points":[{"x":10000,"y":-5},{"x":100,"y":200}]}]}]}
            """);

        await Prompt("outline");

        UserSession session = await _store.GetAsync(UserId);
        Domain.Layout.LayoutLine line = Assert.Single(session.Edits.Layout!.Lines);
        Assert.Equal(new Domain.Layout.LayoutPoint(640, 0), line.Points[0]);
        Assert.Equal(new Domain.Layout.LayoutPoint(100, 200), line.Points[1]);
    }

    [Fact]
    public async Task SessionsStoredRotationSeedsTheSnapshotFrameForClamping()
    {
        await SeedImage();                       // 640x480
        await _editing.RotateAsync(UserId, 1);   // the model was shown a 480x640 snapshot
        Reply("""{"reply":"ok","actions":[{"op":"layout","lines":[{"points":[{"x":500,"y":600}]}]}]}""");

        await Prompt("outline");

        UserSession session = await _store.GetAsync(UserId);
        Domain.Layout.LayoutLine line = Assert.Single(session.Edits.Layout!.Lines);
        Assert.Equal(new Domain.Layout.LayoutPoint(480, 600), line.Points[0]);
    }

    [Fact]
    public async Task VariantLayoutRemapsThroughTheVariantsOwnPrecedingSteps()
    {
        await SeedImage();   // 640x480
        Reply(
            """
            {"reply":"v","actions":[],"variants":[{"label":"lined","actions":[
              {"op":"rotate","dir":"right","times":1},
              {"op":"layout","lines":[{"points":[{"x":100,"y":40},{"x":0,"y":0}]}]}]}]}
            """);

        PromptOutcome outcome = await Prompt("one rotated lined take");

        Assert.Single(outcome.Renders);
        // The variant's render was handed the remapped layout: (x,y) -> (480-y, x).
        string json = await File.ReadAllTextAsync(_cli.LastRequest!.LayoutPath!);
        Domain.Layout.StencilLayout layout = System.Text.Json.JsonSerializer
            .Deserialize<Domain.Layout.StencilLayout>(json, Domain.Serialization.StencilJson.Options)!;
        Domain.Layout.LayoutLine line = Assert.Single(layout.Lines);
        Assert.Equal(new Domain.Layout.LayoutPoint(440, 100), line.Points[0]);
        Assert.Equal(new Domain.Layout.LayoutPoint(480, 0), line.Points[1]);
        // The session itself keeps no layout — variants never mutate it.
        UserSession session = await _store.GetAsync(UserId);
        Assert.Null(session.Edits.Layout);
    }

    // ── §2.1 multi-image ops ──
    // One prompt run carries ONE image (an album is batched one run per photo in the adapter),
    // so index 1 is that image and anything higher is a per-action warning; `save` goes through
    // the active server project, the bot's own save path.

    /// <summary>A PromptService whose §2.1 `save` can reach the mock collaboration server.</summary>
    private PromptService WithProjects(RecordingServerService projects) =>
        new(_llm, _editing, _store, new LlmOptions(), new MockServerClientFactory(),
            new LlmAttachmentLoader(new MockImageDownscaler()), projects);

    /// <summary>Give the session an active server project, as /create or /fetch leaves it.</summary>
    private async Task SeedActiveProject(string name = "cat")
    {
        UserSession session = await _store.GetAsync(UserId);
        await _store.SaveAsync(session with
        {
            ActiveServerUrl = "http://localhost:8090",
            ActiveProjectId = "p1",
            ActiveProjectName = name,
        });
    }

    [Fact]
    public async Task ImageIndexOneRestartsTheWorkingImageAndResetsTheCoordinateFrame()
    {
        await SeedImage();
        Reply(
            """
            {"reply":"one at a time","actions":[
              {"op":"crop","spec":{"x1":"10%"}},
              {"op":"image","index":1},
              {"op":"layout","lines":[{"points":[{"x":5,"y":7}]}]}]}
            """);

        PromptOutcome outcome = await Prompt("crop it, then start over and mark the corner");

        Assert.Empty(outcome.Warnings);
        UserSession session = await _store.GetAsync(UserId);
        // The crop belonged to the image the switch replaced — it is gone…
        Assert.Null(session.Edits.CropSpec);
        // …and the layout that follows lands in the FRESH image's frame, unshifted by it.
        Domain.Layout.LayoutLine line = Assert.Single(session.Edits.Layout!.Lines);
        Assert.Equal(new Domain.Layout.LayoutPoint(5, 7), line.Points[0]);
    }

    [Fact]
    public async Task ImageIndexBeyondThisTurnsAttachmentsWarnsAndTheRestStillRuns()
    {
        await SeedImage();
        Reply(
            """
            {"reply":"both","actions":[{"op":"image","index":3},{"op":"filter","mode":"sepia"}]}
            """);

        PromptOutcome outcome = await Prompt("make them both sepia");

        Assert.Contains(outcome.Warnings, w => w.Contains("attached image 3"));
        Assert.True(outcome.Mutated);
        // The action was skipped, not the plan: the filter still landed on this turn's image.
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("sepia", session.Edits.Filter);
    }

    [Fact]
    public async Task SaveGoesThroughTheActiveServerProjectAndRenamesWhenTheModelNamesIt()
    {
        await SeedImage();
        await SeedActiveProject();
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"saved","actions":[{"op":"filter","mode":"bw"},{"op":"save","name":"portrait 1"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "make it b&w and save it", null, CancellationToken.None);

        Assert.Empty(outcome.Warnings);
        Assert.Equal(["portrait 1"], projects.Renames);
        Assert.Equal(["portrait 1"], projects.Saves);
    }

    [Fact]
    public async Task AnUnnamedSaveKeepsTheActiveProjectsOwnName()
    {
        await SeedImage();
        await SeedActiveProject();
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"saved","actions":[{"op":"save"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "save it", null, CancellationToken.None);

        Assert.Empty(outcome.Warnings);
        Assert.Empty(projects.Renames);
        Assert.Single(projects.Saves);
        // Saving changed no pixels, so the caller has nothing new to send back.
        Assert.False(outcome.Mutated);
    }

    [Fact]
    public async Task ASavePathIsNotedAndTheSaveStillGoesToTheUsualPlace()
    {
        await SeedImage();
        await SeedActiveProject();
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"saved","actions":[{"op":"save","path":"~/Downloads"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "save it to ~/Downloads", null, CancellationToken.None);

        // §10: the destination cannot be honoured here — noted, and the save still ran.
        Assert.Contains(outcome.Warnings, w => w.Contains("cannot save to a path"));
        Assert.Single(projects.Saves);
    }

    [Fact]
    public async Task SaveWithoutAnActiveServerProjectIsAWarningNotAFailedPlan()
    {
        await SeedImage();
        Reply("""{"reply":"saved","actions":[{"op":"rotate","dir":"right"},{"op":"save"}]}""");

        PromptOutcome outcome = await Prompt("rotate and save it");

        Assert.Contains(outcome.Warnings, w => w.Contains("no active server project"));
        Assert.Equal("saved", outcome.Reply);
        // The rest of the plan still applied.
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(1, session.Edits.Rotate);
    }

    [Fact]
    public async Task SaveWithNoWorkingImageIsSkippedWithAWarning()
    {
        // A blank starts the plan (so the pre-flight passes), then the save runs on it.
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"nothing","actions":[{"op":"save","name":"x"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "save it", null, CancellationToken.None);

        // No image at all: the plan-level pre-flight speaks first and nothing was saved.
        Assert.Empty(projects.Saves);
        Assert.Contains("no working image", outcome.Reply);
    }

    [Fact]
    public async Task AServerRejectedSaveIsAWarningAndTheTurnStillReplies()
    {
        await SeedImage();
        await SeedActiveProject();
        RecordingServerService projects = new() { FailWith = "version conflict" };
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"saved","actions":[{"op":"save"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "save it", null, CancellationToken.None);

        Assert.Contains(outcome.Warnings, w => w.Contains("version conflict"));
        Assert.Equal("saved", outcome.Reply);
    }

    // ── §10 connection ops (bot profile) ──
    // `connect` resolves ONLY against the connections the user saved with /connect — exact
    // URL, else unique host — with the STORED token riding along; the model can never
    // introduce a new host or mint a credential. Misses are warnings, never failed plans.

    /// <summary>Store connections as /connect leaves them (normalised origins + stored tokens).</summary>
    private async Task SeedConnections(params ServerConnectionInfo[] connections)
    {
        UserSession session = await _store.GetAsync(UserId);
        await _store.SaveAsync(session with { Connections = connections });
    }

    private static ServerConnectionInfo Saved(string url, string token = "") =>
        new() { Url = url, Token = token };

    [Fact]
    public async Task ConnectResolvesASavedServerByExactUrlAndItsStoredTokenRides()
    {
        await SeedImage();
        await SeedConnections(Saved("http://alpha:8090", "tok-alpha"), Saved("https://beta:9090", "tok-beta"));
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"connected","actions":[{"op":"connect","server":"http://alpha:8090"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "connect to alpha", null, CancellationToken.None);

        Assert.Empty(outcome.Warnings);
        Assert.Equal([("http://alpha:8090", "tok-alpha", true)], projects.Connects);
        // Managing connections changed no pixels — nothing for the caller to render.
        Assert.False(outcome.Mutated);
    }

    [Fact]
    public async Task ConnectResolvesAUniqueHostMatchLikeTheEditorsDo()
    {
        await SeedImage();
        await SeedConnections(Saved("http://alpha:8090", "ta"), Saved("https://beta:9090", "tb"));
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"ok","actions":[{"op":"connect","server":"beta"},{"op":"connect","server":"alpha:8090"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "connect to beta, then alpha", null, CancellationToken.None);

        Assert.Empty(outcome.Warnings);
        Assert.Equal([("https://beta:9090", "tb", true), ("http://alpha:8090", "ta", true)], projects.Connects);
    }

    [Fact]
    public async Task ConnectToAServerTheUserNeverSavedIsAWarningNotAnAttempt()
    {
        await SeedImage();
        await SeedConnections(Saved("http://alpha:8090", "ta"));
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"connecting","actions":[{"op":"connect","server":"http://evil.example:9"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "connect", null, CancellationToken.None);

        // No REST call left the bot — an unsaved host is never contacted.
        Assert.Empty(projects.Connects);
        Assert.Contains(outcome.Warnings, w => w.Contains("connect it first with /connect"));
        Assert.Equal("connecting", outcome.Reply); // a warning, not a failed plan
    }

    [Fact]
    public async Task ConnectWithAnAmbiguousHostWarnsThatTheFullUrlIsNeeded()
    {
        await SeedImage();
        await SeedConnections(Saved("http://srv:8090", "t1"), Saved("https://srv:9090", "t2"));
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"ok","actions":[{"op":"connect","server":"srv"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "connect to srv", null, CancellationToken.None);

        Assert.Empty(projects.Connects);
        Assert.Contains(outcome.Warnings, w => w.Contains("matches several") && w.Contains("full URL"));
    }

    [Fact]
    public async Task DisconnectResolvesAgainstTheConnectionsAndForgetsThatServer()
    {
        await SeedImage();
        await SeedConnections(Saved("http://alpha:8090"), Saved("https://beta:9090"));
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"dropped","actions":[{"op":"disconnect","server":"beta"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "disconnect beta", null, CancellationToken.None);

        Assert.Empty(outcome.Warnings);
        Assert.Equal(["https://beta:9090"], projects.Disconnects);
        Assert.False(outcome.Mutated);
    }

    [Fact]
    public async Task DisconnectingAServerThatIsntConnectedIsAWarning()
    {
        await SeedImage();
        await SeedConnections(Saved("http://alpha:8090"));
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"dropped","actions":[{"op":"disconnect","server":"gamma"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "disconnect gamma", null, CancellationToken.None);

        Assert.Empty(projects.Disconnects);
        Assert.Contains(outcome.Warnings, w => w.Contains("isn't a connected server"));
        Assert.Equal("dropped", outcome.Reply);
    }

    [Fact]
    public async Task ConnectionOpsRunWithoutAWorkingImage()
    {
        // "Connect to my server" must work before any photo is sent — the image pre-flight
        // only guards ops that touch pixels.
        await SeedConnections(Saved("http://alpha:8090", "ta"));
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"connected","actions":[{"op":"connect","server":"alpha"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "connect to alpha", null, CancellationToken.None);

        Assert.Equal("connected", outcome.Reply);
        Assert.Empty(outcome.Warnings);
        Assert.Equal([("http://alpha:8090", "ta", true)], projects.Connects);
        Assert.False(outcome.Mutated);
    }

    [Fact]
    public async Task ARefusedConnectIsAWarningAndTheTurnStillReplies()
    {
        await SeedImage();
        await SeedConnections(Saved("http://alpha:8090", "ta"));
        RecordingServerService projects = new() { ConnectFailWith = "token expired" };
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"connected","actions":[{"op":"connect","server":"alpha"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "connect to alpha", null, CancellationToken.None);

        Assert.Contains(outcome.Warnings, w => w.Contains("token expired"));
        Assert.Equal("connected", outcome.Reply);
    }

    [Fact]
    public async Task ContextSuffixListsConnectionUrlsAndTheActiveProjectButNeverATokens()
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
    public async Task ContextSuffixSaysSoWhenThereAreNoConnections()
    {
        await SeedImage();
        Reply("chat");
        await Prompt("am I connected to anything?");

        string system = _llm.Requests[^1].System!;
        Assert.Contains("no collaboration-server connections", system);
        Assert.DoesNotContain("Active server project", system);
    }

    [Fact]
    public async Task AMultiImageLayoutPlanRunsSilentlyInOneRound()
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
    public async Task ConfiguredServerUrlIsNormalizedLikeTheConnectionsItMatches()
    {
        UserSession session = await _store.GetAsync(UserId);
        await _store.SaveAsync(session with
        {
            // Stored as /connect stores it: the factory-normalised origin.
            Connections = [new ServerConnectionInfo { Url = "http://localhost:8090", Token = "local-tok" }],
        });
        PromptService service = new(_llm, _editing, _store, new LlmOptions
        {
            Provider = LlmOptions.ProviderStencilServer,
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

    [Fact]
    public async Task UndoStepsBackThroughTheEditHistoryLikeTheUndoCommand()
    {
        await SeedImage();
        await _editing.RotateAsync(UserId, 1);
        await _editing.SetFilterAsync(UserId, "bw");
        Reply("""{"reply":"undone","actions":[{"op":"undo","steps":2}]}""");

        PromptOutcome outcome = await Prompt("undo both of those");

        Assert.Empty(outcome.Warnings);
        Assert.True(outcome.Mutated); // the caller re-renders the stepped-back state
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(0, session.Edits.Rotate);
        Assert.Null(session.Edits.Filter);
        Assert.Equal(2, session.EditRedo.Count); // both undos landed on the redo stack
    }

    [Fact]
    public async Task UndoBeyondTheHistoryIsANoteNeverAFailedPlan()
    {
        await SeedImage();
        await _editing.RotateAsync(UserId, 1);
        Reply("""{"reply":"undone","actions":[{"op":"undo","steps":5}]}""");

        PromptOutcome outcome = await Prompt("undo everything");

        Assert.Contains(outcome.Warnings, w => w.Contains("stopped after 1 step"));
        Assert.Equal("undone", outcome.Reply);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(0, session.Edits.Rotate); // the one available step still ran
    }

    [Fact]
    public async Task UndoWithAnEmptyHistorySaysNothingToUndo()
    {
        await SeedImage();
        Reply("""{"reply":"ok","actions":[{"op":"undo"}]}""");

        PromptOutcome outcome = await Prompt("undo");

        Assert.Contains(outcome.Warnings, w => w.Contains("Nothing to undo"));
        Assert.Equal("ok", outcome.Reply);
    }

    [Fact]
    public async Task RedoReappliesTheMostRecentlyUndoneEdit()
    {
        await SeedImage();
        await _editing.RotateAsync(UserId, 1);
        await _editing.UndoAsync(UserId);
        Reply("""{"reply":"redone","actions":[{"op":"redo"}]}""");

        PromptOutcome outcome = await Prompt("redo that");

        Assert.Empty(outcome.Warnings);
        Assert.True(outcome.Mutated);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal(1, session.Edits.Rotate);
    }

    [Fact]
    public async Task ResetDropsEveryPendingEditAndKeepsTheWorkingImage()
    {
        await SeedImage();
        await _editing.RotateAsync(UserId, 1);
        await _editing.SetFilterAsync(UserId, "sepia");
        Reply("""{"reply":"reset","actions":[{"op":"reset"}]}""");

        PromptOutcome outcome = await Prompt("start over with this picture");

        Assert.Empty(outcome.Warnings);
        Assert.True(outcome.Mutated);
        UserSession session = await _store.GetAsync(UserId);
        Assert.True(session.HasImage);
        Assert.True(session.Edits.IsEmpty);
        Assert.Empty(session.EditHistory);
        Assert.Empty(session.EditRedo);
    }

    // ── §10 clear (image + edits ONLY — the conversation survives) ──

    [Fact]
    public async Task ClearRemovesTheImageAndEditsButTheConversationSurvives()
    {
        await SeedImage();
        Reply("chat");
        await Prompt("hello there");
        Reply("""{"reply":"removed","actions":[{"op":"clear"}]}""");

        PromptOutcome outcome = await Prompt("remove the image");

        Assert.Empty(outcome.Warnings);
        // Nothing left to render — the caller must NOT go through render-and-send.
        Assert.False(outcome.Mutated);
        UserSession session = await _store.GetAsync(UserId);
        Assert.False(session.HasImage);
        Assert.True(session.Edits.IsEmpty);
        // §10: the clear is scoped to the image and edits — the chat history is intact
        // (BOTH turns; clearing IT is the separate, user-confirmed clearChat op).
        Assert.Equal(4, _service.BuildChatDocument(UserId)!.Messages.Count);
    }

    [Fact]
    public async Task ClearWithoutAWorkingImageIsANote()
    {
        Reply("""{"reply":"nothing there","actions":[{"op":"clear"}]}""");

        PromptOutcome outcome = await Prompt("remove the image");

        Assert.Contains(outcome.Warnings, w => w.Contains("no working image"));
        Assert.Equal("nothing there", outcome.Reply);
    }

    // ── §10 clearChat (deferred, outcome-level — the Bot layer confirms and clears) ──

    [Fact]
    public async Task ClearChatOnlySetsTheDeferredRequestAndClearsNothingItself()
    {
        await SeedImage();
        Reply("chat");
        await Prompt("hello there");
        Reply("""{"reply":"asking to clear","actions":[{"op":"clearChat"}]}""");

        PromptOutcome outcome = await Prompt("clear this conversation");

        Assert.True(outcome.ClearChatRequested);
        Assert.False(outcome.Mutated);   // no pixels changed — nothing to render-and-send
        Assert.Empty(outcome.Warnings);
        // Nothing is cleared at this level: the history still holds BOTH turns — the caller
        // shows the in-app confirm and runs the /chat clear flow only on the user's yes.
        Assert.Equal(4, _service.BuildChatDocument(UserId)!.Messages.Count);
    }

    [Fact]
    public async Task ClearChatOrderedFirstStillRunsTheOtherActionsAndDefersTheClear()
    {
        await SeedImage();
        Reply("""{"reply":"bw then clear","actions":[{"op":"clearChat"},{"op":"filter","mode":"bw"}]}""");

        PromptOutcome outcome = await Prompt("make it bw and clear the chat");

        // The edit executed regardless of the op's plan position; the clear rides ONLY as the
        // outcome-level request, surfaced after every action applied.
        Assert.True(outcome.ClearChatRequested);
        Assert.True(outcome.Mutated);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("bw", session.Edits.Filter);
        Assert.NotNull(_service.BuildChatDocument(UserId));   // history intact until the confirm
    }

    [Fact]
    public async Task ClearChatRunsWithoutAWorkingImage()
    {
        Reply("""{"reply":"sure","actions":[{"op":"clearChat"}]}""");

        PromptOutcome outcome = await Prompt("clear the conversation");

        Assert.Equal("sure", outcome.Reply);
        Assert.Empty(outcome.Warnings);
        Assert.True(outcome.ClearChatRequested);
    }

    // ── §10 lineStyle (the pen-default /color /thickness /points /style /fill paths) ──

    [Fact]
    public async Task LineStyleConfiguresThePenDefaultsWithoutTouchingThePixels()
    {
        await SeedImage();
        int baseline = _cli.EditCalls;
        Reply(
            """
            {"reply":"pen set","actions":[{"op":"lineStyle","color":"#00ff00","thickness":3,
              "pointSize":6,"style":"dashed","fillColor":"transparent"}]}
            """);

        PromptOutcome outcome = await Prompt("draw green dashed from now on");

        Assert.Empty(outcome.Warnings);
        Assert.False(outcome.Mutated); // pen defaults change no pixels — nothing to send
        Assert.Equal(baseline, _cli.EditCalls);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("#00ff00", session.Edits.Pen.Color);
        Assert.Equal(3, session.Edits.Pen.Thickness);
        Assert.Equal(6, session.Edits.Pen.PointSize);
        Assert.Equal("dashed", session.Edits.Pen.Style);
        Assert.Equal("transparent", session.Edits.Pen.FillColor);
    }

    [Fact]
    public async Task LineStyleFieldsTheBotDoesNotModelAreNotedAndSkipped()
    {
        await SeedImage();
        Reply(
            """
            {"reply":"pen set","actions":[{"op":"lineStyle","color":"#112233",
              "pointColor":"#445566","drawMode":"rect"}]}
            """);

        PromptOutcome outcome = await Prompt("point colour and rect mode");

        Assert.Contains(outcome.Warnings, w => w.Contains("pointColor"));
        Assert.Contains(outcome.Warnings, w => w.Contains("drawMode"));
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("#112233", session.Edits.Pen.Color); // the supported field still landed
    }

    // ── §10 openUrl (the /url path: SSRF vetting + user-echo guard + awaited load) ──

    /// <summary>A public TEST-NET IP literal: the SSRF guard passes it without touching DNS.</summary>
    private const string EchoUrl = "http://203.0.113.9/cat.png";

    [Fact]
    public async Task OpenUrlLoadsTheLinkTheUserWroteAndLaterActionsActOnTheFetchedImage()
    {
        Reply($$"""{"reply":"loaded","actions":[{"op":"openUrl","url":"{{EchoUrl}}"},{"op":"filter","mode":"bw"}]}""");

        PromptOutcome outcome = await Prompt($"load {EchoUrl} and make it b&w");

        Assert.Empty(outcome.Warnings);
        Assert.True(outcome.Mutated);
        // The load was AWAITED before the filter ran: the fetch went through the CLI…
        Assert.Equal(EchoUrl, _cli.LastRequest!.Input);
        UserSession session = await _store.GetAsync(UserId);
        Assert.True(session.HasImage);
        // …and the filter survived, proving it was folded AFTER the load's edit reset —
        // an unawaited load would have wiped it (SetImageFromUrl resets the edit state).
        Assert.Equal("bw", session.Edits.Filter);
        Assert.Equal("cat.png", session.ImageLabel);
    }

    [Fact]
    public async Task OpenUrlTheUserNeverWroteFailsTheWholePlan()
    {
        await SeedImage();
        int baseline = _cli.EditCalls;
        Reply($$"""{"reply":"loading","actions":[{"op":"openUrl","url":"{{EchoUrl}}"},{"op":"filter","mode":"bw"}]}""");

        PromptOutcome outcome = await Prompt("make it black and white"); // the URL appears nowhere

        // §10 echo guard: the model introduced a host — the WHOLE plan fails, nothing ran.
        Assert.Contains("never wrote", outcome.Reply);
        Assert.Contains("Nothing was changed", outcome.Reply);
        Assert.False(outcome.Mutated);
        Assert.Equal(baseline, _cli.EditCalls); // no fetch ever left the bot
        UserSession session = await _store.GetAsync(UserId);
        Assert.Null(session.Edits.Filter);
    }

    [Fact]
    public async Task OpenUrlEchoedInAnEarlierTurnStillCounts()
    {
        Reply("chat");
        await Prompt($"remember this link: {EchoUrl}");
        Reply($$"""{"reply":"loaded","actions":[{"op":"openUrl","url":"{{EchoUrl}}"}]}""");

        PromptOutcome outcome = await Prompt("now load that image");

        Assert.Empty(outcome.Warnings);
        Assert.Equal(EchoUrl, _cli.LastRequest!.Input);
        Assert.True(outcome.Mutated);
    }

    [Fact]
    public async Task OpenUrlKeepsTheUrlPathsSsrfVettingAsANote()
    {
        await SeedImage();
        string local = "http://127.0.0.1/secret.png";
        int baseline = _cli.EditCalls;
        Reply($$"""{"reply":"loading","actions":[{"op":"openUrl","url":"{{local}}"}]}""");

        PromptOutcome outcome = await Prompt($"load {local}");

        // Echoed by the user, so the plan runs — but the /url SSRF guard still blocks the
        // fetch itself, as a per-action note.
        Assert.Contains(outcome.Warnings, w => w.Contains("private or local"));
        Assert.Equal(baseline, _cli.EditCalls);
    }

    [Fact]
    public async Task OpenUrlIncognitoIsIgnoredWithANote()
    {
        Reply($$"""{"reply":"loaded","actions":[{"op":"openUrl","url":"{{EchoUrl}}","incognito":true}]}""");

        PromptOutcome outcome = await Prompt($"open {EchoUrl} privately");

        Assert.Contains(outcome.Warnings, w => w.Contains("incognito"));
        UserSession session = await _store.GetAsync(UserId);
        Assert.True(session.HasImage); // loaded normally
    }

    [Fact]
    public async Task OpenUrlPlanContinuesOnceWithTheFetchedImage()
    {
        PromptService service = WithAttachments();
        Reply($$"""{"reply":"loaded","actions":[{"op":"openUrl","url":"{{EchoUrl}}"}]}""");
        Reply("""{"reply":"outlined","actions":[]}""");

        PromptOutcome outcome = await service.PromptAsync(
            UserId, $"load {EchoUrl} and tell me what's in it", null, CancellationToken.None);

        // §7 auto-continuation: openUrl loaded a picture the model never saw.
        Assert.Equal(2, _llm.Requests.Count);
        Assert.Contains("[The working image is now", _llm.Requests[^1].Messages[^1].Text);
        Assert.Equal("outlined", outcome.Reply);
    }

    // ── §10 renameProject / describe / blankColor / projectColor ──

    [Fact]
    public async Task RenameProjectRenamesTheActiveServerProject()
    {
        await SeedImage();
        await SeedActiveProject();
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"renamed","actions":[{"op":"renameProject","name":"Poster draft"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "rename it to Poster draft", null, CancellationToken.None);

        Assert.Empty(outcome.Warnings);
        Assert.Equal(["Poster draft"], projects.Renames);
        Assert.False(outcome.Mutated); // metadata only — nothing to render
    }

    [Fact]
    public async Task RenameProjectRelabelsAnUnsavedWorkingImage()
    {
        await SeedImage();
        Reply("""{"reply":"renamed","actions":[{"op":"renameProject","name":"cat sketch"}]}""");

        PromptOutcome outcome = await Prompt("call it cat sketch");

        Assert.Empty(outcome.Warnings);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("cat sketch", session.ImageLabel); // the /create default name
    }

    [Fact]
    public async Task ARefusedRenameIsANoteNeverAFailedPlan()
    {
        await SeedImage();
        await SeedActiveProject();
        RecordingServerService projects = new() { RenameFailWith = "name already taken" };
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"renamed","actions":[{"op":"renameProject","name":"dup"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "rename to dup", null, CancellationToken.None);

        Assert.Contains(outcome.Warnings, w => w.Contains("name already taken"));
        Assert.Equal("renamed", outcome.Reply);
    }

    [Fact]
    public async Task DescribeWritesThroughToTheServerAndEmptyClears()
    {
        await SeedImage();
        await SeedActiveProject();
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply(
            """
            {"reply":"described","actions":[{"op":"describe","text":"a poster"},{"op":"describe","text":""}]}
            """);

        PromptOutcome outcome = await service.PromptAsync(UserId, "describe then clear", null, CancellationToken.None);

        Assert.Empty(outcome.Warnings);
        Assert.Equal(["a poster", ""], projects.Descriptions);
        Assert.False(outcome.Mutated);
    }

    [Fact]
    public async Task DescribeHoldsTheTextLocallyWithoutAServerProject()
    {
        await SeedImage();
        Reply("""{"reply":"described","actions":[{"op":"describe","text":"holiday shot"}]}""");

        PromptOutcome outcome = await Prompt("describe it");

        Assert.Empty(outcome.Warnings);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("holiday shot", session.ActiveProjectDescription); // uploaded on /create
    }

    [Fact]
    public async Task BlankColorRecoloursOnlyABlankServerProject()
    {
        await SeedImage();
        await SeedActiveProject();
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"recoloured","actions":[{"op":"blankColor","color":"#dbeafe"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "make the background blue", null, CancellationToken.None);

        Assert.Empty(outcome.Warnings);
        Assert.Equal(["#dbeafe"], projects.BlankColors);
    }

    [Fact]
    public async Task BlankColorOnANonBlankProjectIsANote()
    {
        await SeedImage();
        await SeedActiveProject();
        RecordingServerService projects = new() { BlankColorResult = "" };
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"recoloured","actions":[{"op":"blankColor","color":"#dbeafe"}]}""");

        PromptOutcome outcome = await service.PromptAsync(UserId, "recolour it", null, CancellationToken.None);

        Assert.Contains(outcome.Warnings, w => w.Contains("not a blank"));
        Assert.Empty(projects.BlankColors);
    }

    [Fact]
    public async Task ProjectColorSetsAndClearsButNeedsAnActiveProject()
    {
        await SeedImage();
        await SeedActiveProject();
        RecordingServerService projects = new();
        PromptService service = WithProjects(projects);
        Reply("""{"reply":"tinted","actions":[{"op":"projectColor","color":"#ec4899"},{"op":"projectColor","color":""}]}""");
        PromptOutcome outcome = await service.PromptAsync(UserId, "pink name, then clear it", null, CancellationToken.None);
        Assert.Empty(outcome.Warnings);
        Assert.Equal(["#ec4899", ""], projects.ProjectColors);

        // Without a project: a note, never a failed plan.
        RecordingServerService none = new();
        PromptService bare = WithProjects(none);
        UserSession session = await _store.GetAsync(UserId);
        await _store.SaveAsync(session with { ActiveProjectId = null, ActiveServerUrl = null });
        Reply("""{"reply":"tinted","actions":[{"op":"projectColor","color":"#ec4899"}]}""");
        PromptOutcome missing = await bare.PromptAsync(UserId, "pink name", null, CancellationToken.None);
        Assert.Contains(missing.Warnings, w => w.Contains("no active server project"));
        Assert.Empty(none.ProjectColors);
    }

    // ── §10 export (the /json and /project documents, one send per action) ──

    [Fact]
    public async Task ExportProducesOneDocumentPerActionWithTheSlashCommandsShapes()
    {
        await SeedImage();
        Reply("""{"reply":"here","actions":[{"op":"export","what":"layout"},{"op":"export","what":"project"}]}""");

        PromptOutcome outcome = await Prompt("send me the layout and the project file");

        Assert.Empty(outcome.Warnings);
        Assert.False(outcome.Mutated); // exports change no pixels
        Assert.Equal(2, outcome.Exports.Count);
        Assert.EndsWith(".json", outcome.Exports[0].FileName);
        Assert.Equal("Layout JSON", outcome.Exports[0].Caption);
        Assert.Contains("\"imageWidth\"", System.Text.Encoding.UTF8.GetString(outcome.Exports[0].Bytes));
        Assert.EndsWith(".stencil", outcome.Exports[1].FileName);
        Assert.Equal("Stencil project", outcome.Exports[1].Caption);
        Assert.NotEmpty(outcome.Exports[1].Bytes);
    }

    [Fact]
    public async Task ExportWithoutAWorkingImageIsANote()
    {
        Reply("""{"reply":"here","actions":[{"op":"export","what":"layout"}]}""");

        PromptOutcome outcome = await Prompt("send me the layout");

        Assert.Contains(outcome.Warnings, w => w.Contains("no working image"));
        Assert.Empty(outcome.Exports);
    }

    // ── §2 widened forms in the executor ──

    [Fact]
    public async Task FormulaEnabledFalseClearsBothAxes()
    {
        await SeedImage();
        Reply(
            """
            {"reply":"set","actions":[{"op":"formula","axis":"x","expr":"x*2"},
              {"op":"formula","axis":"y","expr":"y+1"}]}
            """);
        await Prompt("set both formulas");
        Reply("""{"reply":"off","actions":[{"op":"formula","enabled":false}]}""");

        await Prompt("turn formulas off");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Null(session.Edits.FormulaX);
        Assert.Null(session.Edits.FormulaY);
    }

    [Fact]
    public async Task FormulaEmptyExprClearsThatAxisOnly()
    {
        await SeedImage();
        Reply(
            """
            {"reply":"set","actions":[{"op":"formula","axis":"x","expr":"x*2"},
              {"op":"formula","axis":"y","expr":"y+1"}]}
            """);
        await Prompt("set both formulas");
        Reply("""{"reply":"cleared","actions":[{"op":"formula","axis":"x","expr":""}]}""");

        await Prompt("clear the x formula");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Null(session.Edits.FormulaX);
        Assert.Equal("y+1", session.Edits.FormulaY);
    }

    [Fact]
    public async Task PageCustomDimsSetTheCustomPageSize()
    {
        await SeedImage();
        Reply("""{"reply":"sized","actions":[{"op":"page","width":20,"height":30}]}""");

        await Prompt("make the page 20 by 30 cm");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("custom", session.Edits.PageFormat);
        Assert.Equal(20, session.Edits.CustomPageWidth);
        Assert.Equal(30, session.Edits.CustomPageHeight);
    }

    [Fact]
    public async Task BlankWithCmDimsMakesTheCanvasThatSizeInPixels()
    {
        Reply("""{"reply":"made","actions":[{"op":"blank","color":"#ffffff","width":10,"height":15}]}""");

        PromptOutcome outcome = await Prompt("a 10x15 cm blank page");

        Assert.True(outcome.Mutated);
        // The dims rode as the custom page size, converted cm→px like the CLI console
        // (cm / 2.54 * 96, rounded): 10cm → 378px, 15cm → 567px.
        Assert.Equal(378, _cli.LastRequest!.Blank!.Width);
        Assert.Equal(567, _cli.LastRequest.Blank.Height);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("custom", session.Edits.PageFormat);
        Assert.Equal(10, session.Edits.CustomPageWidth);
        Assert.Equal(15, session.Edits.CustomPageHeight);
    }

    // ── the widened §10 context suffix ──

    [Fact]
    public async Task ContextSuffixCarriesPenDefaultsAndThePendingEditStackSize()
    {
        await SeedImage();
        await _editing.ConfigurePenAsync(UserId, "#00ff00", 3, null, "dashed", null);
        await _editing.RotateAsync(UserId, 1);
        await _editing.SetFilterAsync(UserId, "bw");
        await _editing.UndoAsync(UserId);
        Reply("chat");

        await Prompt("what's my pen?");

        string system = _llm.Requests[^1].System!;
        Assert.Contains("Pen defaults for new lines: color #00ff00, thickness 3", system);
        Assert.Contains("style dashed", system);
        // rotate+filter pushed two snapshots; the undo moved one onto the redo stack.
        Assert.Contains("Pending edits: 1 undoable step(s), 1 redoable.", system);
    }

    [Fact]
    public async Task ContextSuffixListsProjectNamesCappedAtTwentyPerServer()
    {
        await SeedImage();
        await SeedConnections(Saved("http://alpha:8090", "ta"));
        RecordingServerService projects = new()
        {
            Projects = Enumerable.Range(1, 22)
                .Select(i => new Application.Servers.ServerProjectInfo(
                    new Domain.Projects.ProjectRecord { Id = $"p{i}", Name = $"proj-{i:00}" },
                    "http://alpha:8090"))
                .ToList(),
        };
        PromptService service = WithProjects(projects);
        Reply("chat");

        await service.PromptAsync(UserId, "what's on my server?", null, CancellationToken.None);

        string system = _llm.Requests[^1].System!;
        Assert.Contains("Projects on http://alpha:8090: proj-01", system);
        Assert.Contains("proj-20", system);
        Assert.Contains("(+2 more)", system);
        Assert.DoesNotContain("proj-21", system);
    }

    [Fact]
    public async Task AnUnreachableListingOmitsTheProjectsLineAndNeverFailsTheTurn()
    {
        await SeedImage();
        await SeedConnections(Saved("http://alpha:8090", "ta"));
        // Projects stays null — the mock's ListProjectsAsync throws, like an unreachable server.
        PromptService service = WithProjects(new RecordingServerService());
        Reply("chat");

        PromptOutcome outcome = await service.PromptAsync(UserId, "hi", null, CancellationToken.None);

        Assert.Equal("chat", outcome.Reply);
        Assert.DoesNotContain("Projects on", _llm.Requests[^1].System);
    }
}
