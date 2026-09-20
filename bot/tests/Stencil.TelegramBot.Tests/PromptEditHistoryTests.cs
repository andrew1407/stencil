using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Tests;

/// <summary>§2/§10 history ops: undo, redo, reset, clear (image and edits only) and the deferred clearChat confirm.</summary>
public sealed class PromptEditHistoryTests : PromptServiceTestBase
{
    public PromptEditHistoryTests(PromptServiceFixture fixture) : base(fixture) { }

    [Fact]
    public async Task Should_Step_Back_Through_The_Edit_History_On_Undo_Like_The_Undo_Command()
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
    public async Task Should_Note_Rather_Than_Fail_The_Plan_On_Undo_Beyond_The_History()
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
    public async Task Should_Say_Nothing_To_Undo_On_Undo_With_An_Empty_History()
    {
        await SeedImage();
        Reply("""{"reply":"ok","actions":[{"op":"undo"}]}""");

        PromptOutcome outcome = await Prompt("undo");

        Assert.Contains(outcome.Warnings, w => w.Contains("Nothing to undo"));
        Assert.Equal("ok", outcome.Reply);
    }

    [Fact]
    public async Task Should_Reapply_The_Most_Recently_Undone_Edit_On_Redo()
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
    public async Task Should_Drop_Every_Pending_Edit_And_Keep_The_Working_Image_On_Reset()
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
    public async Task Should_Remove_The_Image_And_Edits_On_Clear_While_The_Conversation_Survives()
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
    public async Task Should_Note_On_Clear_Without_A_Working_Image()
    {
        Reply("""{"reply":"nothing there","actions":[{"op":"clear"}]}""");

        PromptOutcome outcome = await Prompt("remove the image");

        Assert.Contains(outcome.Warnings, w => w.Contains("no working image"));
        Assert.Equal("nothing there", outcome.Reply);
    }

    // ── §10 clearChat (deferred, outcome-level — the Bot layer confirms and clears) ──

    [Fact]
    public async Task Should_Only_Set_The_Deferred_Request_And_Clear_Nothing_Itself_On_Clear_Chat()
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
    public async Task Should_Still_Run_The_Other_Actions_And_Defer_The_Clear_When_Clear_Chat_Is_Ordered_First()
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
    public async Task Should_Run_Clear_Chat_Without_A_Working_Image()
    {
        Reply("""{"reply":"sure","actions":[{"op":"clearChat"}]}""");

        PromptOutcome outcome = await Prompt("clear the conversation");

        Assert.Equal("sure", outcome.Reply);
        Assert.Empty(outcome.Warnings);
        Assert.True(outcome.ClearChatRequested);
    }

    // ── §10 lineStyle (the pen-default /color /thickness /points /style /fill paths) ──
}
