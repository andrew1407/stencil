using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Tests.Doubles;

namespace Stencil.TelegramBot.Tests;

/// <summary>§10 project metadata: rename, describe, the two colours, and the <c>export</c> documents the caller sends into the chat.</summary>
public sealed class PromptMetadataTests : PromptServiceTestBase
{
    public PromptMetadataTests(PromptServiceFixture fixture) : base(fixture) { }

    [Fact]
    public async Task Should_Rename_The_Active_Server_Project_On_Rename_Project()
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
    public async Task Should_Relabel_An_Unsaved_Working_Image_On_Rename_Project()
    {
        await SeedImage();
        Reply("""{"reply":"renamed","actions":[{"op":"renameProject","name":"cat sketch"}]}""");

        PromptOutcome outcome = await Prompt("call it cat sketch");

        Assert.Empty(outcome.Warnings);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("cat sketch", session.ImageLabel); // the /create default name
    }

    [Fact]
    public async Task Should_Note_Rather_Than_Fail_The_Plan_When_A_Rename_Is_Refused()
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
    public async Task Should_Write_Through_To_The_Server_On_Describe_And_Clear_On_Empty()
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
    public async Task Should_Hold_The_Text_Locally_On_Describe_Without_A_Server_Project()
    {
        await SeedImage();
        Reply("""{"reply":"described","actions":[{"op":"describe","text":"holiday shot"}]}""");

        PromptOutcome outcome = await Prompt("describe it");

        Assert.Empty(outcome.Warnings);
        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("holiday shot", session.ActiveProjectDescription); // uploaded on /create
    }

    [Fact]
    public async Task Should_Recolour_Only_A_Blank_Server_Project_On_Blank_Color()
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
    public async Task Should_Note_On_Blank_Color_For_A_Non_Blank_Project()
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
    public async Task Should_Set_And_Clear_Project_Color_But_Need_An_Active_Project()
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
    public async Task Should_Produce_One_Document_Per_Export_Action_With_The_Slash_Commands_Shapes()
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
    public async Task Should_Note_On_Export_Without_A_Working_Image()
    {
        Reply("""{"reply":"here","actions":[{"op":"export","what":"layout"}]}""");

        PromptOutcome outcome = await Prompt("send me the layout");

        Assert.Contains(outcome.Warnings, w => w.Contains("no working image"));
        Assert.Empty(outcome.Exports);
    }

    // ── §2 widened forms in the executor ──
}
