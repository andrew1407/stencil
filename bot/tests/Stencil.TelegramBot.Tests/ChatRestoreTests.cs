using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Tests.Doubles;
using System.Text.Json;
using System.Text;
using Telegram.Bot.Requests;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// The other half of §12: history seeded back from a fetched project's document, the
/// default-off guarantee, and the once-only warning a failed push earns.
/// </summary>
public sealed class ChatRestoreTests : ChatPersistenceTestBase
{
    [Fact]
    public async Task FetchWithSavingOnSeedsTheHistoryFromTheStoredDocument()
    {
        ServerClient.Files[(ProjectId, ProjectFileKind.Chat)] = Encoding.UTF8.GetBytes(
            """
            {"version":1,"savedAt":1753900000000,"messages":[
              {"role":"user","text":"crop 10% off the left"},
              {"role":"assistant","text":"Done — anything else?"}]}
            """);
        await Send("/chat save on");

        await OpenProjectAsync();

        Assert.Contains("Restored 2 saved chat messages", Messages.Last().Text);

        await Send("/prompt what did we do so far?");

        LlmChatRequest request = _llm.Requests[^1];
        Assert.Equal(3, request.Messages.Count);
        Assert.Equal(LlmMessage.RoleUser, request.Messages[0].Role);
        Assert.Equal("crop 10% off the left", request.Messages[0].Text);
        Assert.Equal(LlmMessage.RoleAssistant, request.Messages[1].Role);
        Assert.Equal("Done — anything else?", request.Messages[1].Text);
        Assert.Equal("what did we do so far?", request.Messages[2].Text);
    }

    [Fact]
    public async Task FetchWithoutAStoredChatRestoresNothingAndStillLoads()
    {
        await Send("/chat save on");

        await OpenProjectAsync();

        Assert.Contains("Loaded project 'Poster'", Messages.Last().Text);
        Assert.DoesNotContain("Restored", Messages.Last().Text);
    }

    [Fact]
    public async Task WithSavingOffNoServerFileCallEverHappens()
    {
        ServerClient.Files[(ProjectId, ProjectFileKind.Chat)] = Encoding.UTF8.GetBytes(
            """{"version":1,"messages":[{"role":"user","text":"old"}]}""");

        await OpenProjectAsync();               // SaveChats defaults off
        Assert.DoesNotContain("Restored", Messages.Last().Text);

        await Send("/prompt make it nicer");
        await Send("/chat clear");

        Assert.Empty(ChatPuts);
        Assert.Empty(ServerClient.FileDeletes);
        // The stored chat was not loaded either: the next turn starts fresh.
        await Send("/prompt hi");
        Assert.Single(_llm.Requests[^1].Messages, m => m.Role == LlmMessage.RoleUser && m.Text == "hi");
    }

    [Fact]
    public async Task ASaveFailureWarnsOnceKeepsTheReplyAndRearmsOnSuccess()
    {
        await OpenProjectAsync();
        await Send("/chat save on");
        ServerClient.ThrowOnPutKind = ProjectFileKind.Chat;

        await Send("/prompt one");
        await Send("/prompt two");

        // Both replies were delivered (the default plan answers "ok")…
        Assert.Equal(2, Messages.Count(m => m.Text == "ok"));
        // …and the soft failure was surfaced exactly once, not per turn.
        Assert.Single(Messages, m => m.Text.Contains("Couldn't store the conversation"));

        ServerClient.ThrowOnPutKind = null;
        await Send("/prompt three");

        Assert.Single(ChatPuts);
        Assert.Single(Messages, m => m.Text.Contains("Couldn't store the conversation"));
    }
}
