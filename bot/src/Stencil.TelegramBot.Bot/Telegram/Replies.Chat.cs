using System.Text;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Bot.Telegram;

// Replies — the assistant's own wording: chat mode, the /prompt turn and the chat-API picker.
// Class doc lives in Replies.cs.
public static partial class Replies
{
    public static string ChatModeOn() => BotStrings.Reply("chatModeOn");

    public static string ChatModeOff() => BotStrings.Reply("chatModeOff");

    /// <summary>
    /// Confirmation for <c>/chat clear</c> — the assistant's conversation is forgotten; chat mode
    /// itself is untouched, which the reply spells out when it is on.
    /// </summary>
    public static string ChatHistoryCleared(bool chatModeOn) =>
        BotStrings.Reply(chatModeOn ? "chatClearedModeOn" : "chatCleared");

    public static string PromptWorking() => BotStrings.Reply("promptWorking");

    /// <summary>
    /// A turn ended by the ⏹ Stop button: asked for and delivered, so a notice, never an error.
    /// Ops already applied stay (/undo walks them back) and a 🔄 Retry button rides along.
    /// </summary>
    public static string PromptStopped() => Tag(Tone.Notice, BotStrings.Reply("promptStopped"));

    public static string PromptStopping() => Tag(Tone.Notice, BotStrings.Reply("promptStopping"));

    public static string ChatApiNoProfiles() => Tag(Tone.Notice, BotStrings.Reply("chatApiNoProfiles"));

    public static string ChatApiList(IReadOnlyList<LlmProfile> profiles, LlmProfile? current)
    {
        StringBuilder sb = new();
        sb.Append(BotStrings.Reply("chatApiListHeader"));
        foreach (LlmProfile p in profiles)
        {
            sb.Append(BotStrings.Reply(p.Name == current?.Name ? "chatApiListCurrentPrefix" : "chatApiListOtherPrefix"));
            sb.Append(p.Label).Append(BotStrings.Reply("chatApiListSeparator")).Append(p.Summary());
        }
        sb.Append(BotStrings.Reply("chatApiListNowUsing")).Append(current?.Label ?? BotStrings.Reply("chatApiListDefault"));
        return sb.ToString();
    }

    public static string ChatApiSelected(LlmProfile picked) =>
        Tag(Tone.Success, BotStrings.Reply("chatApiSelected", picked.Label, picked.Summary()));

    public static string ChatApiUnknown(string wanted, IReadOnlyList<LlmProfile> profiles) => Tag(
        Tone.Error, BotStrings.Reply("chatApiUnknown", wanted, string.Join(", ", profiles.Select(p => p.Name))));

    /// <summary>
    /// The §10 <c>clearChat</c> in-app confirmation, sent at the END of the plan's turn — the
    /// model can ask, but only the user's Yes button clears anything.
    /// </summary>
    public static string ClearChatConfirm() => BotStrings.Reply("clearChatConfirm");

    public static string ClearChatCanceled() => BotStrings.Reply("clearChatCanceled");

    public static string ChatUsage() => BotStrings.Reply("chatUsage");

    public static string ChatSaveOn() => BotStrings.Reply("chatSaveOn");

    /// <summary>Confirmation for <c>/chat save off</c> (§12.2: no retroactive delete).</summary>
    public static string ChatSaveOff() => BotStrings.Reply("chatSaveOff");

    public static string ChatSaveStatus(bool on) =>
        BotStrings.Reply(on ? "chatSaveStatusOn" : "chatSaveStatusOff");

    public static string ChatRestored(int count) =>
        BotStrings.Reply(count == 1 ? "chatRestoredOne" : "chatRestoredMany", count);

    public static string ChatSaveFailed() => Tag(Tone.Warning, BotStrings.Reply("chatSaveFailed"));

    public static string SourcesHelp() => BotStrings.Reply("sourcesHelp");

    public static string DrawHelp() => BotStrings.Reply("drawHelp");

    /// <summary>The filter variants for a bare <c>/filter</c> (mirrors the CLI console's list).</summary>
    public static string FilterVariants() => BotStrings.Reply("filterVariants");

    public static string RotateVariants() => BotStrings.Reply("rotateVariants");

    public static string CropUsage() => BotStrings.Reply("cropUsage");

    public static string ConnectUsage() => BotStrings.Reply("connectUsage");

}
