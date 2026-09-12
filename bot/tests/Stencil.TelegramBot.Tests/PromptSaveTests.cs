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
/// §2.1 <c>image</c> and <c>save</c>: switching the working image, and persisting through the
/// active server project — every miss a warning, never a failed plan.
/// </summary>
public sealed class PromptSaveTests : PromptServiceTestBase
{
    public PromptSaveTests(PromptServiceFixture fixture) : base(fixture) { }

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

}
