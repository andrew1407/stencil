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
/// §2 <c>formula</c>, <c>page</c> and <c>blank</c> dimensions, plus the context suffix lines the
/// pen defaults and project listings contribute.
/// </summary>
public sealed class PromptPageTests : PromptServiceTestBase
{
    public PromptPageTests(PromptServiceFixture fixture) : base(fixture) { }

    [Fact]
    public async Task Should_Clear_Both_Axes_When_Formula_Enabled_Is_False()
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
    public async Task Should_Clear_That_Axis_Only_When_A_Formula_Expr_Is_Empty()
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
    public async Task Should_Set_The_Custom_Page_Size_From_Page_Custom_Dims()
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
    public async Task Should_Make_The_Canvas_That_Size_In_Pixels_For_Blank_With_Cm_Dims()
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
    public async Task Should_Carry_Pen_Defaults_And_The_Pending_Edit_Stack_Size_In_The_Context_Suffix()
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
    public async Task Should_List_Project_Names_Capped_At_Twenty_Per_Server_In_The_Context_Suffix()
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
    public async Task Should_Omit_The_Projects_Line_And_Never_Fail_The_Turn_When_The_Listing_Is_Unreachable()
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