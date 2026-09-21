using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Domain.Editing;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Domain.Llm.Wire;

namespace Stencil.TelegramBot.Tests.Llm.Prompt;

/// <summary>What the model is shown: the §7 edge map attached beside the working image, and the single auto-continuation round a load-only plan earns.</summary>
public sealed class PromptVisionTests : PromptServiceTestBase
{
    public PromptVisionTests(PromptServiceFixture fixture) : base(fixture) { }

    [Fact]
    public async Task Should_Attach_The_Edge_Map_Second_And_The_Suffix_Sentence_On_A_Working_Image_Turn()
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
    public async Task Should_Carry_Neither_Edge_Map_Nor_Sentence_On_A_Text_Only_Turn()
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
    public async Task Should_Silently_Skip_An_Edge_Map_Render_Failure()
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
    public async Task Should_Never_Replay_The_Edge_Map_As_The_Prior_Turns_Image()
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

    // §7 auto-continuation: a plan whose actions contain a load op (blank/frame) and drew NO layout planned
    // blind, so the turn is re-sent ONCE with the fresh working image attached.

    [Fact]
    public async Task Should_Continue_Once_With_The_Fresh_Image_For_A_Mixed_Load_Plan_Without_A_Layout()
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
    public async Task Should_Replay_The_Continuation_Note_But_Never_Persist_It()
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
    public void Should_Refuse_Machinery_From_A_Dirty_Document_In_Seed_History()
    {
        // A document from another surface (or an older build) may still carry §7 internals;
        // restoring must not put them back into the replayed conversation (§12.1).
        ChatDocument dirty = new()
        {
            Messages =
            [
                new ChatDocumentMessage("user", "[The working image is now the frame — carry on.]"),
                new ChatDocumentMessage("user", "crop it\n\n" + ChatDocument.CONTINUATION_NOTE),
                new ChatDocumentMessage("assistant", """{"version":1,"reply":"Cropped.","actions":[]}"""),
                new ChatDocumentMessage("assistant", "Cropped."),
            ],
        };

        Assert.Equal(2, _service.SeedHistory(UserId, dirty));
        ChatDocument doc = _service.BuildChatDocument(UserId)!;
        Assert.Equal(["crop it", "Cropped."], doc.Messages.Select(m => m.Text));
    }

    [Fact]
    public async Task Should_Not_Continue_A_Load_Plan_That_Drew_A_Layout()
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
    public async Task Should_Bound_Continuation_To_A_Single_Round_Per_Turn()
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
}
