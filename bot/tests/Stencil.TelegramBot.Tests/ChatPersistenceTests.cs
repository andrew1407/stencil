using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Projects;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Tests.Doubles;
using System.Text.Json;
using System.Text;
using Telegram.Bot.Requests;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Opt-in per-project chat persistence (§12): the <c>/chat save</c> toggle and its 💾 button,
/// the after-turn push of the §12.1 document, and the delete that rides <c>/chat clear</c>.
/// </summary>
public sealed class ChatPersistenceTests : ChatPersistenceTestBase
{
    [Fact]
    public async Task ChatSaveOnPersistsTheFlagAndOffTurnsItBack()
    {
        await Send("/chat save on");
        Assert.True((await _store.GetAsync(UserId)).SaveChats);
        Assert.Contains("Chat saving on", Messages.Last().Text);
        // §12.2: the bot's only store IS the server project, so turning this on must
        // say that anyone the project is shared with can read the conversation.
        Assert.Contains("shared with", Messages.Last().Text);

        await Send("/chat save");
        Assert.Contains("Chat saving is on", Messages.Last().Text);
        Assert.Contains("shared with", Messages.Last().Text);

        await Send("/chat save off");
        Assert.False((await _store.GetAsync(UserId)).SaveChats);
        Assert.Contains("Chat saving off", Messages.Last().Text);
    }

    /// <summary>
    /// §12.2 requires the sharing consequence to be stated wherever the toggle is offered
    /// — not only on the confirmation once it is already on. A transcript records what the
    /// user asked for in their own words, and the bot's only store IS the server project,
    /// so it inherits that project's access. The two affordances the confirmation test does
    /// not reach: the status a user reads BEFORE opting in (the moment the disclosure has
    /// to land), and the 💾 button, which must not be a quieter path to the same decision.
    /// </summary>
    [Fact]
    public async Task EveryChatSaveAffordanceSaysWhoCanReadTheTranscript()
    {
        await Send("/chat save");
        Assert.False((await _store.GetAsync(UserId)).SaveChats); // still the default
        Assert.Contains("Chat saving is off", Messages.Last().Text);
        Assert.Contains("shared with", Messages.Last().Text);

        await Tap("chat:save-on");
        Assert.True((await _store.GetAsync(UserId)).SaveChats);
        Assert.Contains("shared with", Messages.Last().Text);
    }

    [Fact]
    public async Task TheSaveButtonTogglesTheFlagAndTheMenuShowsTheState()
    {
        await Tap("chat:save-on");
        Assert.True((await _store.GetAsync(UserId)).SaveChats);

        // Entering chat mode now shows the toggle reflecting the ON state (tap turns it off).
        await Send("/chat");
        SendMessageRequest confirm = Messages.Last();
        InlineKeyboardMarkup markup = Assert.IsType<InlineKeyboardMarkup>(confirm.ReplyMarkup);
        InlineKeyboardButton save = Assert.Single(
            markup.InlineKeyboard.SelectMany(r => r), b => b.CallbackData!.StartsWith("chat:save-"));
        Assert.Equal("💾 Save chats: on", save.Text);
        Assert.Equal("chat:save-off", save.CallbackData);

        await Tap("chat:save-off");
        Assert.False((await _store.GetAsync(UserId)).SaveChats);
    }

    [Fact]
    public async Task APromptTurnPushesTheDisplayedReplyDocumentToTheChatKind()
    {
        await OpenProjectAsync();
        await Send("/chat save on");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"version":1,"reply":"Made it black & white.","actions":[{"op":"filter","mode":"bw"}]}"""));

        await Send("/prompt make it bw");

        var put = Assert.Single(ChatPuts);
        Assert.Equal(ProjectId, put.Id);
        Assert.Equal("json", put.Ext);
        using JsonDocument doc = JsonDocument.Parse(Encoding.UTF8.GetString(put.Data));
        Assert.Equal(1, doc.RootElement.GetProperty("version").GetInt32());
        Assert.True(doc.RootElement.GetProperty("savedAt").GetInt64() > 0);
        JsonElement[] messages = doc.RootElement.GetProperty("messages").EnumerateArray().ToArray();
        Assert.Equal(2, messages.Length);
        Assert.Equal("user", messages[0].GetProperty("role").GetString());
        Assert.Equal("make it bw", messages[0].GetProperty("text").GetString());
        Assert.Equal("assistant", messages[1].GetProperty("role").GetString());
        // The DISPLAYED reply is persisted (contract §12.1), never the raw JSON plan…
        Assert.Equal("Made it black & white.", messages[1].GetProperty("text").GetString());
        // …and the document is text-only: the vision attachment never reaches the server.
        Assert.DoesNotContain("images", Encoding.UTF8.GetString(put.Data));
        // §9: the chat upload is filestore-only — the project version was not bumped.
        Assert.Equal(3, (await ServerClient.GetProjectAsync(ProjectId)).Project.Version);
    }

    [Fact]
    public async Task ChatClearWithSavingOnDeletesTheServerCopy()
    {
        await OpenProjectAsync();
        await Send("/chat save on");
        await Send("/prompt hello there");
        Assert.Single(ChatPuts);

        await Send("/chat clear");

        Assert.Contains((ProjectId, ProjectFileKind.CHAT), ServerClient.FileDeletes);
        Assert.Contains("Conversation cleared", Messages.Last().Text);
    }
}
