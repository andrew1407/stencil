using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Stencil.TelegramBot.Tests.Doubles;
using Telegram.Bot.Requests;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// Chat mode's toggle and routing: the session flag on/off through <c>/chat</c> and the 💬
/// buttons, plain text taking the same prompt path (variants included), and commands never
/// swallowed by it.
/// </summary>
public sealed class ChatModeTests : ChatModeTestBase
{
    [Fact]
    public async Task Should_Turn_The_Mode_On_Confirm_And_Offer_The_Way_Back_On_Chat_Command()
    {
        await Send("/chat");

        UserSession session = await _store.GetAsync(UserId);
        Assert.True(session.ChatMode);
        SendMessageRequest confirm = Assert.Single(Messages);
        Assert.Contains("Chat mode on", confirm.Text);
        Assert.Contains("/chat off", confirm.Text);
        InlineKeyboardMarkup markup = Assert.IsType<InlineKeyboardMarkup>(confirm.ReplyMarkup);
        Assert.Contains(markup.InlineKeyboard.SelectMany(row => row), b => b.CallbackData == "chat:off");
    }

    [Fact]
    public async Task Should_Turn_The_Mode_Back_Off_And_Confirm_On_Chat_Off()
    {
        await Send("/chat");
        await Send("/chat off");

        UserSession session = await _store.GetAsync(UserId);
        Assert.False(session.ChatMode);
        Assert.Contains("Chat mode off", Messages.Last().Text);
    }

    [Fact]
    public async Task Should_Toggle_The_Same_Flag_As_The_Command_From_The_Buttons()
    {
        await Tap("chat:on");
        Assert.True((await _store.GetAsync(UserId)).ChatMode);

        await Tap("chat:off");
        Assert.False((await _store.GetAsync(UserId)).ChatMode);
    }

    [Fact]
    public void Should_Surface_The_Chat_Button_In_The_Main_Menu_And_Edit_Menu()
    {
        Assert.Contains(Keyboards.MainMenu().InlineKeyboard.SelectMany(r => r), b => b.CallbackData == "chat:on");
        Assert.Contains(Keyboards.EditMenu(false).InlineKeyboard.SelectMany(r => r), b => b.CallbackData == "chat:on");
    }

    [Fact]
    public async Task Should_Just_Explain_The_Command_And_Change_Nothing_For_An_Unknown_Argument()
    {
        await Send("/chat whenever");

        Assert.False((await _store.GetAsync(UserId)).ChatMode);
        Assert.Contains("/chat off to stop", Assert.Single(Messages).Text);
    }

    [Fact]
    public async Task Should_Load_An_Image_Link_With_Instructions_Then_Run_Them_Through_The_Assistant()
    {
        await Send("/chat");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"reply":"Made it black & white.","actions":[{"op":"filter","mode":"bw"}]}"""));

        await Send("upload image b&w, highlight face and head: https://example.com/a.jpg");

        // The link still loads as the working image (rendered back to the chat)…
        Assert.NotEmpty(_bot.Requests.OfType<SendPhotoRequest>());
        // …with an explicit "still editing" note before the slow assistant turn (the /url
        // render's edit menu otherwise reads as "done").
        Assert.Contains(Messages, m => m.Text.Contains("now editing"));
        // …and the words around it reached the assistant, WITHOUT the URL in the prompt text.
        LlmChatRequest turn = Assert.Single(_llm.Requests);
        string sent = turn.Messages.Last().Text;
        Assert.Contains("highlight face and head", sent);
        Assert.DoesNotContain("https://example.com/a.jpg", sent);
        Assert.Equal("bw", _cli.LastRequest!.Filter);
    }

    [Fact]
    public async Task Should_Still_Just_Load_A_Bare_Image_Link_Even_In_Chat_Mode()
    {
        await Send("/chat");

        await Send("https://example.com/a.jpg");

        Assert.NotEmpty(_bot.Requests.OfType<SendPhotoRequest>());
        Assert.Empty(_llm.Requests);   // nothing to ask about — the link is the whole message
    }

    [Fact]
    public async Task Should_Keep_Loading_Only_The_Link_For_A_Link_With_Words_Outside_Chat_Mode()
    {
        await Send("look at this https://example.com/a.jpg");

        Assert.NotEmpty(_bot.Requests.OfType<SendPhotoRequest>());
        Assert.Empty(_llm.Requests);
    }

    [Fact]
    public async Task Should_Send_Plain_Text_To_The_Assistant_And_Render_Like_Prompt_In_Chat_Mode()
    {
        await Send("/blank");
        await Send("/chat");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """{"reply":"Made it black & white.","actions":[{"op":"filter","mode":"bw"}]}"""));

        await Send("make it black and white");

        Assert.Single(_llm.Requests);
        Assert.Equal("Made it black & white.", Messages.Last().Text);
        Assert.Equal("bw", _cli.LastRequest!.Filter);
        // /blank rendered one photo; the chat turn's result is the second.
        Assert.Equal(2, _bot.Requests.OfType<SendPhotoRequest>().Count());
        // Chat mode stays on until it is switched off.
        Assert.True((await _store.GetAsync(UserId)).ChatMode);
    }

    [Fact]
    public async Task Should_Still_Return_Variants_As_One_Media_Group_In_Chat_Mode()
    {
        await Send("/blank");
        await Send("/chat");
        _llm.CannedReplies.Enqueue(new LlmReply(
            """
            {"reply":"Two takes.","actions":[],"variants":[
              {"label":"rotated","actions":[{"op":"rotate","dir":"left"}]},
              {"label":"sepia","actions":[{"op":"filter","mode":"sepia"}]}]}
            """));

        await Send("give me two variants");

        SendMediaGroupRequest album = Assert.Single(_bot.Requests.OfType<SendMediaGroupRequest>());
        Assert.Equal(2, album.Media.Count());
    }

    [Fact]
    public async Task Should_Still_Hint_On_Plain_Text_And_Never_Reach_The_Llm_Without_Chat_Mode()
    {
        await Send("make it black and white");

        Assert.Empty(_llm.Requests);
        Assert.Contains("Send a photo or an image link", Assert.Single(Messages).Text);
    }

    [Fact]
    public async Task Should_Never_Swallow_Commands_In_Chat_Mode()
    {
        await Send("/chat");

        await Send("/blank");

        // The command rendered a blank canvas instead of being sent to the assistant…
        Assert.Empty(_llm.Requests);
        Assert.Single(_bot.Requests.OfType<SendPhotoRequest>());
        UserSession session = await _store.GetAsync(UserId);
        Assert.True(session.HasImage);
        // …and chat mode survived the command.
        Assert.True(session.ChatMode);
    }
}
