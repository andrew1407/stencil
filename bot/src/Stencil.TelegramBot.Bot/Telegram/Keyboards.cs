using Stencil.TelegramBot.Domain.Llm;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Bot.Telegram;

// A button's payload is a short token (≤64 bytes) consumed by CallbackAction; every menu mirrors
// slash commands.
public static partial class Keyboards
{
    // §11: labels live in the session; a tap composes, never edits.
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

    // A token cannot carry the prompt; UserSession.LastRetryablePrompt does.
    public static InlineKeyboardMarkup RetryPrompt() =>
        new(new[] { new[] { BotStrings.Button("retryPrompt") } });

    public static InlineKeyboardMarkup StopPrompt() =>
        new(new[] { new[] { BotStrings.Button("stopPrompt") } });

    // The payload is the profile NAME, never an endpoint — a tap cannot point the bot at an
    // arbitrary host.
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

    // Long option text is clipped to stay readable on a phone.
    private static string Trim(string label) => label.Length <= 40 ? label : label[..39] + "…";

    public static InlineKeyboardMarkup MainMenu() => new(MainRows());

    public static InlineKeyboardMarkup StatusMenu(bool hasActiveProject)
    {
        List<InlineKeyboardButton[]> rows = MainRows();
        if (hasActiveProject)
        {
            rows.AddRange(ProjectActionsRows(hasActiveProject: true));
        }
        return new InlineKeyboardMarkup(rows);
    }

    // Rename and Describe always show (they carry into /create); five abreast squeeze on a phone,
    // hence two rows.
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
                BotStrings.Button("link"),
                BotStrings.Button("expiration"),
                BotStrings.Button("remove"),
            });
        }
        return rows;
    }

    // Destructive, so never on a single tap.
    public static InlineKeyboardMarkup DeleteConfirmMenu() =>
        new(new[]
        {
            new[] { BotStrings.Button("deleteConfirm") },
            new[] { BotStrings.Button("deleteCancel") },
        });

    // §10 clearChat never fires on the model's word alone: Yes rides the same /chat clear path as
    // the 🧹 button.
    public static InlineKeyboardMarkup ClearChatConfirmMenu() =>
        new(new[]
        {
            new[] { BotStrings.Button("clearChatConfirm") },
            new[] { BotStrings.Button("clearChatCancel") },
        });

    // The §12 toggle's label shows the current setting and its token flips it.
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
