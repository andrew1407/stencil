using Stencil.TelegramBot.Domain.Llm;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Inline-keyboard builders. A button's payload is a short token (≤64 bytes) consumed by
/// <see cref="CallbackAction"/>; every menu mirrors the slash commands it dispatches.
/// </summary>
public static partial class Keyboards
{
    /// <summary>The §11 choice card. Labels live in the session; a tap composes, never edits.</summary>
    public static InlineKeyboardMarkup AskCardKeyboard(IReadOnlyList<string> options, bool multi, IReadOnlyList<int> picked, bool allowCustom)
    {
        List<InlineKeyboardButton[]> rows = new();
        for (int i = 0; i < options.Count; i++)
        {
            string tick = multi && picked.Contains(i) ? BotStrings.Mark("askPicked") : multi ? BotStrings.Mark("askUnpicked") : "";
            rows.Add([InlineKeyboardButton.WithCallbackData($"{tick}{Trim(options[i])}", $"ask:{i}")]);
        }
        if (multi)
        {
            rows.Add([BotStrings.Button("askSend")]);
        }
        if (allowCustom)
        {
            // No button: a custom answer IS just typing, which chat mode already forwards.
            rows.Add([BotStrings.Button("askCustom")]);
        }
        return new InlineKeyboardMarkup(rows);
    }

    /// <summary>Re-runs the prompt held in <c>UserSession.LastRetryablePrompt</c> — a token cannot carry it.</summary>
    public static InlineKeyboardMarkup RetryPrompt() =>
        new(new[] { new[] { BotStrings.Button("retryPrompt") } });

    /// <summary>
    /// Calls off a turn that holds the user's gate for minutes — see
    /// <see cref="PromptCancellations"/> for why the tap bypasses that gate.
    /// </summary>
    public static InlineKeyboardMarkup StopPrompt() =>
        new(new[] { new[] { BotStrings.Button("stopPrompt") } });

    /// <summary>
    /// The <c>/chatapi</c> picker. The payload is the profile NAME, never an endpoint — that is
    /// what keeps a tap from pointing the bot at an arbitrary host.
    /// </summary>
    public static InlineKeyboardMarkup ChatApiMenu(IReadOnlyList<LlmProfile> profiles, string? current)
    {
        List<InlineKeyboardButton[]> rows = new();
        foreach (LlmProfile p in profiles)
        {
            string tick = string.Equals(p.Name, current, StringComparison.OrdinalIgnoreCase) ? BotStrings.Mark("chatApiCurrent") : "";
            rows.Add([InlineKeyboardButton.WithCallbackData($"{tick}{Trim(p.Label)}", $"api:{p.Name}")]);
        }
        return new InlineKeyboardMarkup(rows);
    }

    /// <summary>Button labels have to stay readable on a phone — long option text is clipped.</summary>
    private static string Trim(string label) => label.Length <= 40 ? label : label[..39] + "…";

    public static InlineKeyboardMarkup MainMenu() => new(MainRows());

    /// <summary>The /status menu; a server project adds an Expiration entry (<c>exp:menu</c>).</summary>
    public static InlineKeyboardMarkup StatusMenu(bool hasActiveProject)
    {
        List<InlineKeyboardButton[]> rows = MainRows();
        if (hasActiveProject)
        {
            rows.AddRange(ProjectActionsRows(hasActiveProject: true));
        }
        return new InlineKeyboardMarkup(rows);
    }

    /// <summary>
    /// The project-actions rows shared by <see cref="StatusMenu"/> and <see cref="EditMenu"/>.
    /// Rename and Describe always show (they carry into <c>/create</c>); the saved-project-only
    /// three ride a second row, since five abreast squeeze on a phone.
    /// </summary>
    private static List<InlineKeyboardButton[]> ProjectActionsRows(bool hasActiveProject)
    {
        List<InlineKeyboardButton[]> rows = new()
        {
            new[]
            {
                BotStrings.Button("rename"),
                BotStrings.Button("describe"),
            },
        };
        if (hasActiveProject)
        {
            rows.Add(new[]
            {
                // Bare verb token: CallbackAction dispatches it as /link.
                BotStrings.Button("link"),
                BotStrings.Button("expiration"),
                BotStrings.Button("remove"),
            });
        }
        return rows;
    }

    /// <summary>Destructive, so never on a single tap: <c>del:confirm</c> deletes, <c>del:cancel</c> restores.</summary>
    public static InlineKeyboardMarkup DeleteConfirmMenu() =>
        new(new[]
        {
            new[] { BotStrings.Button("deleteConfirm") },
            new[] { BotStrings.Button("deleteCancel") },
        });

    /// <summary>
    /// §10 <c>clearChat</c> never fires on the model's word alone: Yes rides the same
    /// <c>/chat clear</c> path as the 🧹 button, Cancel notes "clear canceled".
    /// </summary>
    public static InlineKeyboardMarkup ClearChatConfirmMenu() =>
        new(new[]
        {
            new[] { BotStrings.Button("clearChatConfirm") },
            new[] { BotStrings.Button("clearChatCancel") },
        });

    /// <summary>
    /// The three things <c>/chat off|clear|save</c> do; the §12 toggle's label shows the current
    /// setting and its token flips it.
    /// </summary>
    public static InlineKeyboardMarkup ChatModeMenu(bool saveChats = false) =>
        new(new[]
        {
            new[]
            {
                BotStrings.Button("chatClear"),
                BotStrings.Button("chatOff"),
            },
            new[]
            {
                BotStrings.Button(saveChats ? "chatSaveOn" : "chatSaveOff"),
            },
        });
}
