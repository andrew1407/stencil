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
/// A plan's shape: chat-only turns, top-level actions folding into the session, variants rendered
/// from state copies, and the per-action warnings a bad plan collects instead of failing.
/// </summary>
public sealed class PromptPlanTests : PromptServiceTestBase
{
    public PromptPlanTests(PromptServiceFixture fixture) : base(fixture) { }

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
        // Both variants rendered from the same base — in parallel, so neither is "the last call".
        EditRequest[] rendered = [.. _cli.Requests.Skip(baseline)];
        Assert.Contains(rendered, r => r.Filter == "sepia" && r.Rotate is null);
        Assert.Contains(rendered, r => r.Rotate == 3 && r.Filter is null);
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
}
