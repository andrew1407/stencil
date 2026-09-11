using Stencil.TelegramBot.Application.Servers;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Bot.Telegram;

// Keyboards — the menu tree a tap walks: the main rows, the edit menu and its submenus, the
// expiration picker and the project list. Class doc lives in Keyboards.cs.
public static partial class Keyboards
{
    private static List<InlineKeyboardButton[]> MainRows() =>
        new()
        {
            // Chat mode gets its own full-width row: it's the entry point to the assistant, and
            // the token is the same command (/chat on) the slash surface exposes.
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

    /// <summary>
    /// The expiry-duration picker: preset spans, a custom free-text entry, and "Never" (keep
    /// forever). Sent as its own message by the <c>/expire</c> command, so each preset just rides a
    /// token mapped to the equivalent <c>/expire &lt;span&gt;</c> command by <see cref="CallbackAction"/>.
    /// </summary>
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

    /// <summary>
    /// The main per-result edit menu sent with a rendered image. Transform / Filter / Draw are
    /// group buttons that open a submenu in place (see <see cref="EditSubmenu"/> etc.); the rest
    /// act directly. No "Result" button — the image is already shown and /save persists it.
    /// </summary>
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
        // The edit menu always rides a rendered image, so Rename is always available here; the
        // server-only Link/Expiration/Remove row rides along only when it's a saved server project
        // (the menu shown after /fetch). Same tokens as StatusMenu.
        rows.AddRange(ProjectActionsRows(hasActiveProject));
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
        rows.Add(BackRow());
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
            BackRow(),
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
            BackRow(),
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
            BackRow(),
        });

    private static InlineKeyboardButton[] BackRow() =>
        new[] { BotStrings.Button("back") };

    /// <summary>One button per project, labelled with its name + server host, callback <c>fetch:&lt;id&gt;</c>.</summary>
    public static InlineKeyboardMarkup ProjectList(IEnumerable<ServerProjectInfo> projects)
    {
        List<InlineKeyboardButton[]> rows = new();
        // Cap the buttons the same way ProjectsText caps its lines — Telegram rejects an oversized
        // keyboard, and the text already tells the user how to reach the rest (/fetch, /projects url).
        foreach (ServerProjectInfo p in projects.Take(Replies.MaxProjectsListed))
        {
            string dot = Replies.ColorDot(p.Record.Color);
            string prefix = dot.Length == 0 ? "" : dot + " ";
            string label = $"{prefix}{p.Record.Name} @ {Replies.Host(p.ServerUrl)}";
            string token = Token($"fetch:{p.Record.Id}");
            rows.Add(new[] { InlineKeyboardButton.WithCallbackData(label, token) });
        }
        return new InlineKeyboardMarkup(rows);
    }

    /// <summary>Clamp a callback payload to Telegram's 64-byte limit.</summary>
    private static string Token(string value) =>
        value.Length <= 64 ? value : value[..64];
}
