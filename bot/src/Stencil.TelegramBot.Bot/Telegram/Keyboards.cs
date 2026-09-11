using Stencil.TelegramBot.Domain.Llm;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Inline-keyboard builders. Every button's callback payload is a short token consumed by
/// <see cref="CallbackAction"/> (kept ≤64 bytes per Telegram's limit). The menus deliberately
/// mirror the slash commands so the chat UI and the command surface stay in lock-step, like
/// the toolbar mirrors the console in the browser front-end.
/// </summary>
public static partial class Keyboards
{
    /// <summary>
    /// The §11 choice card: one button per option (ticked when picked), plus Send on a multi-pick
    /// card. Labels live in the session; a tap only composes the answer, it never edits.
    /// </summary>
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

    /// <summary>
    /// The one-button keyboard on a turn that did not deliver (it failed, or was stopped): re-runs
    /// the same prompt, held in <c>UserSession.LastRetryablePrompt</c> since a token cannot carry it.
    /// </summary>
    public static InlineKeyboardMarkup RetryPrompt() =>
        new(new[] { new[] { BotStrings.Button("retryPrompt") } });

    /// <summary>
    /// The one-button keyboard on the assistant's working notice. A turn can run for minutes and
    /// holds the user's gate while it does, so without this there is no way to call it off —
    /// see <see cref="PromptCancellations"/> for why the tap bypasses that gate.
    /// </summary>
    public static InlineKeyboardMarkup StopPrompt() =>
        new(new[] { new[] { BotStrings.Button("stopPrompt") } });

    /// <summary>
    /// The <c>/chatapi</c> picker: one button per configured chat API, the current one ticked.
    /// The payload carries the profile NAME (operator-defined, short by construction) — never an
    /// endpoint, which is what keeps a tap from pointing the bot at an arbitrary host.
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

    /// <summary>
    /// The main menu, plus an Expiration entry when a server project is active — the button opens
    /// the <see cref="ExpirationMenu"/> in place (token <c>exp:menu</c>). Sent with /status.
    /// </summary>
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
    /// Rename and Describe always show (they carry into <c>/create</c>); Link, Expiration and
    /// Remove need a saved project, so they ride a second row — five abreast squeeze on a phone.
    /// Each token dispatches the equivalent command, so both menus behave alike.
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

    /// <summary>
    /// The delete-project confirmation (destructive, so it never fires on a single tap): a
    /// permanent-delete button (<c>del:confirm</c> → the <c>/delete confirm</c> command) and a
    /// Cancel that restores the status menu in place (<c>del:cancel</c>).
    /// </summary>
    public static InlineKeyboardMarkup DeleteConfirmMenu() =>
        new(new[]
        {
            new[] { BotStrings.Button("deleteConfirm") },
            new[] { BotStrings.Button("deleteCancel") },
        });

    /// <summary>
    /// The §10 <c>clearChat</c> confirmation (the clear never fires on the model's word alone):
    /// Yes rides the same <c>/chat clear</c> path as the 🧹 button (<c>chatclear:confirm</c>);
    /// Cancel retires the prompt with a "clear canceled" note (<c>chatclear:cancel</c>).
    /// </summary>
    public static InlineKeyboardMarkup ClearChatConfirmMenu() =>
        new(new[]
        {
            new[] { BotStrings.Button("clearChatConfirm") },
            new[] { BotStrings.Button("clearChatCancel") },
        });

    /// <summary>
    /// The chat-mode keyboard: the way back out (<c>chat:off</c>), "forget the conversation"
    /// (<c>chat:clear</c>), and the §12 persistence toggle whose label shows the current setting
    /// and whose token flips it — the same three things <c>/chat off|clear|save</c> do.
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
