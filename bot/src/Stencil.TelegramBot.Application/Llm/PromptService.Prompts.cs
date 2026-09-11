namespace Stencil.TelegramBot.Application.Llm;

// PromptService — the system prompts, assembled from the shared asset around the OpRegistry
// sections so a prompt can never promise an op the bot cannot execute. Class doc lives in
// PromptService.cs.
public sealed partial class PromptService
{
    /// <summary>The §10 block's closing sentence — contract prose, not a bullet, so it comes
    /// from the shared asset.</summary>
    private static string BotOpsFooter => SystemPromptAsset.Text("botOpsFooter");

    /// <summary>
    /// The canonical §4 system prompt: shared prose around an "Available ops" section assembled
    /// from <see cref="OpRegistry"/>, so it can never promise an op the bot cannot execute.
    /// </summary>
    public static readonly string SystemPrompt =
        SystemPromptAsset.Head + OpRegistry.CoreOpsSection + SystemPromptAsset.Tail;

    /// <summary>
    /// The bot's §10 profile block — one bullet per §10-scoped op, spliced at the end of §4's op
    /// list where the GUI editors splice theirs. Every op maps 1:1 onto an existing command.
    /// </summary>
    public static readonly string BotOpsPrompt =
        OpRegistry.ProfileOpsSection + "\n" + BotOpsFooter;

    /// <summary>
    /// §4 plus <see cref="BotOpsPrompt"/> at the op list's end. The anchor is verified, so
    /// rewording §4 fails loudly here instead of shipping a prompt with the block missing.
    /// </summary>
    public static readonly string ChatSystemPrompt = BuildChatSystemPrompt();

    private const string BotOpsSpliceAnchor = "\n\nWhen a choice is genuinely";

    private static string BuildChatSystemPrompt()
    {
        int at = SystemPrompt.IndexOf(BotOpsSpliceAnchor, StringComparison.Ordinal);
        return at >= 0
            ? SystemPrompt.Insert(at, "\n" + BotOpsPrompt)
            : throw new InvalidOperationException("SystemPrompt no longer contains the bot-ops splice anchor");
    }

    /// <summary>The §7 edge-map sentence (shared asset prose), appended only when the edge map
    /// is actually attached.</summary>
    public static string EdgeMapSentence => SystemPromptAsset.Text("edgeMapSentence");
}
