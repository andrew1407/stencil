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

/// <summary>The conversation the model sees: the §7 32-message bound, the image-replay rule, idle-user eviction, and the system prompt assembled from the op registry.</summary>
public sealed class PromptHistoryTests : PromptServiceTestBase
{
    public PromptHistoryTests(PromptServiceFixture fixture) : base(fixture) { }

    [Fact]
    public async Task Should_Bound_The_History_To_The_Most_Recent_32_Messages()
    {
        await SeedImage();
        for (int i = 0; i < 25; i++)
        {
            Reply("just chatting");
            await Prompt($"turn {i}");
        }

        // Each turn adds 2 history messages; the request replays at most 32 + the current one.
        LlmChatRequest last = _llm.Requests[^1];
        Assert.Equal(PromptService.MAX_HISTORY_MESSAGES + 1, last.Messages.Count);
        // The oldest turns fell off the front; the newest user message is the current turn.
        Assert.Equal("turn 24", last.Messages[^1].Text);
        Assert.Equal(LlmMessage.ROLE_USER, last.Messages[^1].Role);
    }

    [Fact]
    public async Task Should_Keep_Only_The_Current_Turns_Image_Plus_The_Most_Recent_Prior_One_On_Image_Replay()
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
    public async Task Should_Evict_Idle_Users_Histories_Beyond_The_Tracked_Bound()
    {
        // Fill the registry past the cap: user 0 first, then MaxTrackedUsers more.
        for (long id = 0; id <= PromptService.MAX_TRACKED_USERS; id++)
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
        await _service.PromptAsync(PromptService.MAX_TRACKED_USERS, "again", null, CancellationToken.None);
        Assert.Equal(3, _llm.Requests[^1].Messages.Count);
    }

    [Fact]
    public async Task Should_Build_The_System_Prompt_From_The_Canonical_Constant_With_The_Bot_Ops_Block_And_A_Context_Suffix_Appended()
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
    public void Should_Assemble_The_Prompts_From_The_Op_Registry()
    {
        // §13: no hand-maintained ops block — §4's op list and the §10 profile block are the registry's generated
        // sections verbatim; the prose core around them is the embedded canonical asset.
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
    public void Should_Carry_The_Layout_Tracing_Guidance_In_The_System_Prompt()
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
}
