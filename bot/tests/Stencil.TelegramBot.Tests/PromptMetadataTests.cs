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
/// §10 project metadata: rename, describe, the two colours, and the <c>export</c> documents the
/// caller sends into the chat.
/// </summary>
public sealed class PromptMetadataTests : PromptServiceTestBase
{
    public PromptMetadataTests(PromptServiceFixture fixture) : base(fixture) { }

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
}
