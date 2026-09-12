namespace Stencil.TelegramBot.Application.Llm;

public sealed partial class PromptService
{
    // Contract prose, not a bullet, so it comes from the shared asset.
    private static string BotOpsFooter => SystemPromptAsset.Text("botOpsFooter");

    // Shared prose around an "Available ops" section assembled from OpRegistry.
    public static readonly string SystemPrompt =
        SystemPromptAsset.Head + OpRegistry.CoreOpsSection + SystemPromptAsset.Tail;

    // One bullet per §10-scoped op, spliced at the end of §4's op list where the GUI editors splice
    // theirs.
    public static readonly string BotOpsPrompt =
        OpRegistry.ProfileOpsSection + "\n" + BotOpsFooter;

    // The anchor is verified, so rewording §4 fails loudly instead of shipping a prompt with the
    // block missing.
    public static readonly string ChatSystemPrompt = buildChatSystemPrompt();

    private const string _botOpsSpliceAnchor = "\n\nWhen a choice is genuinely";

    private static string buildChatSystemPrompt()
    {
        int at = SystemPrompt.IndexOf(_botOpsSpliceAnchor, StringComparison.Ordinal);
        return at >= 0
            ? SystemPrompt.Insert(at, "\n" + BotOpsPrompt)
            : throw new InvalidOperationException("SystemPrompt no longer contains the bot-ops splice anchor");
    }

    // Appended only when the edge map is actually attached.
    public static string EdgeMapSentence => SystemPromptAsset.Text("edgeMapSentence");
}
