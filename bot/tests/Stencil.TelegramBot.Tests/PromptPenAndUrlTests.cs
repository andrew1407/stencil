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
/// §10 <c>lineStyle</c> and <c>openUrl</c>: pen defaults that touch no pixels, and the user-echo
/// guard that decides which link may be loaded.
/// </summary>
public sealed class PromptPenAndUrlTests : PromptServiceTestBase
{
    public PromptPenAndUrlTests(PromptServiceFixture fixture) : base(fixture) { }

    [Fact]
    public async Task Should_Configure_The_Pen_Defaults_Without_Touching_The_Pixels_On_Line_Style()
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
    public async Task Should_Note_And_Skip_Line_Style_Fields_The_Bot_Does_Not_Model()
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
    private const string _echoUrl = "http://203.0.113.9/cat.png";

    [Fact]
    public async Task Should_Load_The_Link_The_User_Wrote_On_Open_Url_And_Act_On_The_Fetched_Image_In_Later_Actions()
    {
        Reply($$"""{"reply":"loaded","actions":[{"op":"openUrl","url":"{{_echoUrl}}"},{"op":"filter","mode":"bw"}]}""");

        PromptOutcome outcome = await Prompt($"load {_echoUrl} and make it b&w");

        Assert.Empty(outcome.Warnings);
        Assert.True(outcome.Mutated);
        // The load was AWAITED before the filter ran: the fetch went through the CLI…
        Assert.Equal(_echoUrl, _cli.LastRequest!.Input);
        UserSession session = await _store.GetAsync(UserId);
        Assert.True(session.HasImage);
        // …and the filter survived, proving it was folded AFTER the load's edit reset —
        // an unawaited load would have wiped it (SetImageFromUrl resets the edit state).
        Assert.Equal("bw", session.Edits.Filter);
        Assert.Equal("cat.png", session.ImageLabel);
    }

    [Fact]
    public async Task Should_Fail_The_Whole_Plan_On_Open_Url_The_User_Never_Wrote()
    {
        await SeedImage();
        int baseline = _cli.EditCalls;
        Reply($$"""{"reply":"loading","actions":[{"op":"openUrl","url":"{{_echoUrl}}"},{"op":"filter","mode":"bw"}]}""");

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
    public async Task Should_Still_Count_An_Open_Url_Echoed_In_An_Earlier_Turn()
    {
        Reply("chat");
        await Prompt($"remember this link: {_echoUrl}");
        Reply($$"""{"reply":"loaded","actions":[{"op":"openUrl","url":"{{_echoUrl}}"}]}""");

        PromptOutcome outcome = await Prompt("now load that image");

        Assert.Empty(outcome.Warnings);
        Assert.Equal(_echoUrl, _cli.LastRequest!.Input);
        Assert.True(outcome.Mutated);
    }

    [Fact]
    public async Task Should_Keep_The_Url_Paths_Ssrf_Vetting_As_A_Note_On_Open_Url()
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
    public async Task Should_Ignore_Incognito_With_A_Note_On_Open_Url()
    {
        Reply($$"""{"reply":"loaded","actions":[{"op":"openUrl","url":"{{_echoUrl}}","incognito":true}]}""");

        PromptOutcome outcome = await Prompt($"open {_echoUrl} privately");

        Assert.Contains(outcome.Warnings, w => w.Contains("incognito"));
        UserSession session = await _store.GetAsync(UserId);
        Assert.True(session.HasImage); // loaded normally
    }

    [Fact]
    public async Task Should_Continue_Once_With_The_Fetched_Image_For_An_Open_Url_Plan()
    {
        PromptService service = WithAttachments();
        Reply($$"""{"reply":"loaded","actions":[{"op":"openUrl","url":"{{_echoUrl}}"}]}""");
        Reply("""{"reply":"outlined","actions":[]}""");

        PromptOutcome outcome = await service.PromptAsync(
            UserId, $"load {_echoUrl} and tell me what's in it", null, CancellationToken.None);

        // §7 auto-continuation: openUrl loaded a picture the model never saw.
        Assert.Equal(2, _llm.Requests.Count);
        Assert.Contains("[The working image is now", _llm.Requests[^1].Messages[^1].Text);
        Assert.Equal("outlined", outcome.Reply);
    }

    // ── §10 renameProject / describe / blankColor / projectColor ──
}
