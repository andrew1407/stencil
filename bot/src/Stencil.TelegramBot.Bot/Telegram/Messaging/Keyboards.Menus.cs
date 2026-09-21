using Stencil.TelegramBot.Application.Servers;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Bot.Telegram.Messaging;

public static partial class Keyboards
{
    private static List<InlineKeyboardButton[]> mainRows() =>
        new()
        {
            // Chat mode gets its own full-width row: the entry point to the assistant.
            new[]
            {
                BotStrings.Button("chatWithAssistant"),
            },
            new[]
            {
                BotStrings.Button("help"),
                BotStrings.Button("status"),
            },
            new[]
            {
                BotStrings.Button("sources"),
                BotStrings.Button("blank"),
            },
            new[]
            {
                BotStrings.Button("connect"),
                BotStrings.Button("projects"),
            },
            new[]
            {
                BotStrings.Button("create"),
                BotStrings.Button("save"),
            },
        };

    // Each preset rides a token CallbackAction maps to the equivalent /expire <span>.
    public static InlineKeyboardMarkup ExpirationMenu() =>
        new(new[]
        {
            new[]
            {
                BotStrings.Button("expire1d"),
                BotStrings.Button("expire3d"),
            },
            new[]
            {
                BotStrings.Button("expire1w"),
                BotStrings.Button("expire2w"),
            },
            new[]
            {
                BotStrings.Button("expire1mo"),
                BotStrings.Button("expire3mo"),
            },
            new[]
            {
                BotStrings.Button("expireCustom"),
                BotStrings.Button("expireNever"),
            },
        });

    // No "Result" button — the image is already shown and /save persists it.
    public static InlineKeyboardMarkup EditMenu(bool hasActiveProject)
    {
        List<InlineKeyboardButton[]> rows = new()
        {
            new[]
            {
                BotStrings.Button("menuEdit"),
                BotStrings.Button("menuFilter"),
                BotStrings.Button("menuDraw"),
            },
            new[]
            {
                BotStrings.Button("undo"),
                BotStrings.Button("redo"),
                BotStrings.Button("chat"),
            },
            new[]
            {
                BotStrings.Button("menuDownload"),
                BotStrings.Button("reset"),
                BotStrings.Button("save"),
            },
        };
        // The server-only Link/Expiration/Remove row rides along only for a saved server project.
        // Same tokens as StatusMenu.
        rows.AddRange(projectActionsRows(hasActiveProject));
        return new InlineKeyboardMarkup(rows);
    }

    public static InlineKeyboardMarkup DownloadSubmenu(bool hasEdits)
    {
        List<InlineKeyboardButton[]> rows = new()
        {
            new[]
            {
                BotStrings.Button("image"),
                BotStrings.Button("project"),
            },
        };
        if (hasEdits)
        {
            rows.Add(new[] { BotStrings.Button("layoutJson") });
        }
        rows.Add(backRow());
        return new InlineKeyboardMarkup(rows);
    }

    public static InlineKeyboardMarkup EditSubmenu() =>
        new(new[]
        {
            new[]
            {
                BotStrings.Button("rotatePlus90"),
                BotStrings.Button("rotateMinus90"),
                BotStrings.Button("crop"),
            },
            backRow(),
        });

    public static InlineKeyboardMarkup FilterSubmenu() =>
        new(new[]
        {
            new[]
            {
                BotStrings.Button("filterBw"),
                BotStrings.Button("filterSepia"),
            },
            new[]
            {
                BotStrings.Button("filterInvert"),
                BotStrings.Button("filterContour"),
            },
            new[]
            {
                BotStrings.Button("filterTint"),
                BotStrings.Button("filterNone"),
            },
            backRow(),
        });

    public static InlineKeyboardMarkup DrawSubmenu() =>
        new(new[]
        {
            new[]
            {
                BotStrings.Button("draw"),
                BotStrings.Button("undoLine"),
                BotStrings.Button("clearLines"),
            },
            backRow(),
        });

    private static InlineKeyboardButton[] backRow() =>
        new[] { BotStrings.Button("back") };

    public static InlineKeyboardMarkup ProjectList(IEnumerable<ServerProjectInfo> projects)
    {
        List<InlineKeyboardButton[]> rows = new();
        // Telegram rejects an oversized keyboard; the text already tells the user how to reach the
        // rest.
        foreach (ServerProjectInfo p in projects.Take(Replies.MAX_PROJECTS_LISTED))
        {
            string dot = Replies.ColorDot(p.Record.Color);
            string prefix = dot.Length == 0 ? "" : dot + " ";
            string label = $"{prefix}{p.Record.Name} @ {Replies.Host(p.ServerUrl)}";
            string token = tokenFor($"fetch:{p.Record.Id}");
            rows.Add(new[] { InlineKeyboardButton.WithCallbackData(label, token) });
        }
        return new InlineKeyboardMarkup(rows);
    }

    // Telegram's 64-byte callback limit.
    private static string tokenFor(string value) =>
        value.Length <= 64 ? value : value[..64];
}
