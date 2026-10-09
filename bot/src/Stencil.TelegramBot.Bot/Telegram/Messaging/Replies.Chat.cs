using System.Text;
using Stencil.TelegramBot.Domain.Llm;

namespace Stencil.TelegramBot.Bot.Telegram.Messaging;

public static partial class Replies
{
    public static string ChatModeOn() => BotStrings.Reply("chatModeOn");

    public static string ChatModeOff() => BotStrings.Reply("chatModeOff");

    // Chat mode itself is untouched, which the reply spells out when it is on.
    public static string ChatHistoryCleared(bool chatModeOn) =>
        BotStrings.Reply(chatModeOn ? "chatClearedModeOn" : "chatCleared");

    public static string PromptWorking() => BotStrings.Reply("promptWorking");

    public static string ScriptWorking() => BotStrings.Reply("scriptWorking");

    // Asked for and delivered, so a notice, never an error; ops already applied stay (/undo walks
    // them back).
    public static string PromptStopped() => Tag(Tone.NOTICE, BotStrings.Reply("promptStopped"));

    public static string PromptStopping() => Tag(Tone.NOTICE, BotStrings.Reply("promptStopping"));

    public static string ChatApiNoProfiles() => Tag(Tone.NOTICE, BotStrings.Reply("chatApiNoProfiles"));

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
        Tag(Tone.SUCCESS, BotStrings.Reply("chatApiSelected", picked.Label, picked.Summary()));

    public static string ChatApiUnknown(string wanted, IReadOnlyList<LlmProfile> profiles) => Tag(
        Tone.ERROR, BotStrings.Reply("chatApiUnknown", wanted, string.Join(", ", profiles.Select(p => p.Name))));

    // Sent at the END of the plan's turn — the model can ask, but only the user's Yes button clears
    // anything.
    public static string ClearChatConfirm() => BotStrings.Reply("clearChatConfirm");

    public static string ClearChatCanceled() => BotStrings.Reply("clearChatCanceled");

    public static string ChatUsage() => BotStrings.Reply("chatUsage");

    public static string ChatSaveOn() => BotStrings.Reply("chatSaveOn");

    // §12.2: no retroactive delete.
    public static string ChatSaveOff() => BotStrings.Reply("chatSaveOff");

    public static string ChatSaveStatus(bool on) =>
        BotStrings.Reply(on ? "chatSaveStatusOn" : "chatSaveStatusOff");

    public static string ChatRestored(int count) =>
        BotStrings.Reply(count == 1 ? "chatRestoredOne" : "chatRestoredMany", count);

    public static string ChatSaveFailed() => Tag(Tone.WARNING, BotStrings.Reply("chatSaveFailed"));

    public static string SourcesHelp() => BotStrings.Reply("sourcesHelp");

    public static string DrawHelp() => BotStrings.Reply("drawHelp");

    // Mirrors the CLI console's list.
    public static string FilterVariants() => BotStrings.Reply("filterVariants");

    public static string RotateVariants() => BotStrings.Reply("rotateVariants");

    public static string CropUsage() => BotStrings.Reply("cropUsage");

    public static string ConnectUsage() => BotStrings.Reply("connectUsage");

    public static string UrlUsage() => BotStrings.Reply("urlUsage");

    public static string SourceSiteUsage() => BotStrings.Reply("sourceSiteUsage");

    public static string SourceUploadUsage() => BotStrings.Reply("sourceUploadUsage");

    public static string Scraping(string host) => BotStrings.Reply("scraping", host);

    public static string ScrapedFiles(int count, string host) => BotStrings.Reply("scrapedFiles", count, host);

    public static string OptionUnknown(string option) => BotStrings.Reply("optionUnknown", option);

    public static string OptionNotCount(string key, string value) => BotStrings.Reply("optionNotCount", key, value);

    public static string NoImageAtIndex(int index) => BotStrings.Reply("noImageAtIndex", index);

    public static string PromptUsage() => BotStrings.Reply("promptUsage");

    public static string AskCustomHint(string label) => BotStrings.Reply("askCustomHint", label);

    public static string AskClosed() => BotStrings.Reply("askClosed");

    public static string AskTypeAnswer() => BotStrings.Reply("askTypeAnswer");

    public static string AskPickOne() => BotStrings.Reply("askPickOne");

    public static string NothingRunning() => BotStrings.Reply("nothingRunning");

    public static string RetryStale() => BotStrings.Reply("retryStale");

    public static string RemovalCancelled() => BotStrings.Reply("removalCancelled");

    public static string NameAsk(string current) => BotStrings.Reply("nameAsk", current);

    public static string DescriptionAsk(string? current) => BotStrings.Reply("descriptionAsk", string.IsNullOrEmpty(current) ? BotStrings.Reply("descriptionNone") : BotStrings.Reply("descriptionCurrent", current));

    public static string ThisImage() => BotStrings.Reply("thisImage");
}
