using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Tests;

/// <summary>Forgetting the conversation: <c>/chat clear</c>, the 🧹 button, §10's <c>clearChat</c> plan (which only asks — the user's button clears), <c>/drop</c>, and pending free text.</summary>
public sealed class ChatClearTests : ChatModeTestBase
{
    [Fact]
    public async Task Should_Let_A_Pending_Free_Text_Flow_Win_Over_Chat_Mode()
    {
        await Send("/blank");
        await Send("/chat");
        // The Rename button arms the pending free-text prompt (see CallbackAction).
        await Tap("name:menu");

        await Send("Poster draft");

        UserSession session = await _store.GetAsync(UserId);
        Assert.Equal("Poster draft", session.ImageLabel);
        Assert.Empty(_llm.Requests);
        // The one-shot flow is spent; the next plain message goes to the assistant again.
        Assert.Null(session.PendingInput);
        Assert.True(session.ChatMode);

        _llm.CannedReplies.Enqueue(new LlmReply("Nice name."));
        await Send("what do you think?");
        Assert.Single(_llm.Requests);
        Assert.Equal("Nice name.", Messages.Last().Text);
    }

    [Fact]
    public async Task Should_Forget_The_Conversation_Without_Leaving_Chat_Mode_On_Clear()
    {
        await Send("/blank");
        await Send("/chat");
        await Send("first question");
        await Send("second question");
        // The second turn replayed the first exchange: user, assistant, user.
        Assert.Equal(3, _llm.Requests[^1].Messages.Count);

        await Send("/chat clear");

        Assert.Contains("Conversation cleared", Messages.Last().Text);
        Assert.Contains("Chat mode is still on", Messages.Last().Text);
        UserSession session = await _store.GetAsync(UserId);
        Assert.True(session.ChatMode);
        // The working image survives a clear — only the conversation is forgotten.
        Assert.True(session.HasImage);

        await Send("third question");

        LlmChatRequest fresh = _llm.Requests[^1];
        LlmMessage only = Assert.Single(fresh.Messages);
        Assert.Equal(LlmMessage.ROLE_USER, only.Role);
        Assert.Equal("third question", only.Text);
    }

    [Fact]
    public async Task Should_Run_The_Same_Clear_From_The_Clear_Button()
    {
        await Send("/blank");
        await Send("/chat");
        await Send("first question");

        await Tap("chat:clear");

        Assert.Contains("Conversation cleared", Messages.Last().Text);
        await Send("next question");
        Assert.Single(_llm.Requests[^1].Messages);
        Assert.True((await _store.GetAsync(UserId)).ChatMode);
    }

    // ── §10 clearChat (the model asks; only the user's button clears) ──

    [Fact]
    public async Task Should_Defer_A_Clear_Chat_Plan_To_A_Confirm_Sent_After_The_Reply_And_Edits()
    {
        await Send("/blank");
        await Send("/chat");
        await Send("first question");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"reply":"BW done.","actions":[{"op":"clearChat"},{"op":"filter","mode":"bw"}]}"""));

        await Send("make it bw and clear the chat");

        // The plan's edit ran regardless of clearChat's position…
        Assert.Equal("bw", _cli.LastRequest!.Filter);
        // …and the confirm is the LAST message of the turn, with the Yes/Cancel keyboard.
        SendMessageRequest confirm = Messages.Last();
        Assert.Contains("Clear it?", confirm.Text);
        InlineKeyboardMarkup markup = Assert.IsType<InlineKeyboardMarkup>(confirm.ReplyMarkup);
        Assert.Contains(markup.InlineKeyboard.SelectMany(r => r), b => b.CallbackData == "chatclear:confirm");
        Assert.Contains(markup.InlineKeyboard.SelectMany(r => r), b => b.CallbackData == "chatclear:cancel");
        // Nothing is forgotten until the user answers: the next turn replays every exchange
        // (2 prior exchanges = 4 messages, plus the current one).
        await Send("still here?");
        Assert.Equal(5, _llm.Requests[^1].Messages.Count);
    }

    [Fact]
    public async Task Should_Run_The_Same_Clear_As_Chat_Clear_From_The_Clear_Chat_Yes_Button()
    {
        await Send("/blank");
        await Send("/chat");
        await Send("first question");
        _llm.CannedReplies.Enqueue(new LlmReply("""{"reply":"ok","actions":[{"op":"clearChat"}]}"""));
        await Send("clear this conversation");

        await Tap("chatclear:confirm");

        Assert.Contains("Conversation cleared", Messages.Last().Text);
        // The history really cleared — the next turn starts fresh — and chat mode survives.
        await Send("next question");
        Assert.Single(_llm.Requests[^1].Messages);
        Assert.True((await _store.GetAsync(UserId)).ChatMode);
    }

    [Fact]
    public async Task Should_Keep_The_Conversation_With_A_Canceled_Note_From_The_Clear_Chat_Cancel_Button()
    {
        await Send("/blank");
        await Send("/chat");
        await Send("first question");
        _llm.CannedReplies.Enqueue(new LlmReply("""{"reply":"ok","actions":[{"op":"clearChat"}]}"""));
        await Send("clear this conversation");

        await Tap("chatclear:cancel");

        // The decline is a "clear canceled" note (the confirm edited in place), never an error…
        EditMessageTextRequest note = _bot.Requests.OfType<EditMessageTextRequest>().Last();
        Assert.Contains("Clear canceled", note.Text);
        // …and nothing was forgotten: the next turn still replays every prior exchange.
        await Send("next question");
        Assert.Equal(5, _llm.Requests[^1].Messages.Count);
    }

    [Fact]
    public async Task Should_Also_Forget_The_Conversation_On_Drop()
    {
        await Send("/blank");
        await Send("/chat");
        await Send("first question");

        await Send("/drop");

        Assert.False((await _store.GetAsync(UserId)).HasImage);
        await Send("a brand new question");
        LlmMessage only = Assert.Single(_llm.Requests[^1].Messages);
        Assert.Equal("a brand new question", only.Text);
    }

    [Fact]
    public async Task Should_Keep_Chat_Mode_Per_User_And_Show_It_In_Status()
    {
        await Send("/chat");

        UserSession mine = await _store.GetAsync(UserId);
        UserSession other = await _store.GetAsync(UserId + 1);
        Assert.True(mine.ChatMode);
        Assert.False(other.ChatMode);
        Assert.Contains("Chat mode: on", Replies.StatusText(mine));
        Assert.DoesNotContain("Chat mode", Replies.StatusText(other));
    }
}
