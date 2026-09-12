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
/// §3.0 layout turns: one model round per turn, and the §1 frame mapping that carries traced
/// points through the plan's own crop/rotate steps.
/// </summary>
public sealed class PromptLayoutTests : PromptServiceTestBase
{
    public PromptLayoutTests(PromptServiceFixture fixture) : base(fixture) { }

    private const string _layoutPlan =
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
        Reply(_layoutPlan);

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
    private const string _disjointLayoutPlan =
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
        Reply(_disjointLayoutPlan);

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
        Reply(_layoutPlan);

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
        Reply(_layoutPlan);

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

}
