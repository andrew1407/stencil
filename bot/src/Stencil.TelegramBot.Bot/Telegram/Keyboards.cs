using Stencil.TelegramBot.Application.Servers;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Inline-keyboard builders. Every button's callback payload is a short token consumed by
/// <see cref="CallbackAction"/> (kept ≤64 bytes per Telegram's limit). The menus deliberately
/// mirror the slash commands so the chat UI and the command surface stay in lock-step, like
/// the toolbar mirrors the console in the browser front-end.
/// </summary>
public static class Keyboards
{
    /// <summary>The top-level menu shown after /start and /help.</summary>
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
            rows.Add(ProjectActionsRow(hasActiveProject: true));
        }
        return new InlineKeyboardMarkup(rows);
    }

    /// <summary>
    /// The project-actions row shared by <see cref="StatusMenu"/> and <see cref="EditMenu"/>.
    /// Rename and Describe are always offered (they also apply to a not-yet-saved working image,
    /// carried into <c>/create</c>); Expiration and Remove need a saved server project, so they
    /// appear only when <paramref name="hasActiveProject"/>. Each token dispatches the equivalent
    /// command (which replies with the prompt / picker / confirmation as a fresh message), so they
    /// work identically from either menu.
    /// </summary>
    private static InlineKeyboardButton[] ProjectActionsRow(bool hasActiveProject)
    {
        List<InlineKeyboardButton> buttons = new()
        {
            InlineKeyboardButton.WithCallbackData("✏️ Rename", "name:menu"),
            InlineKeyboardButton.WithCallbackData("📝 Describe", "desc:menu"),
        };
        if (hasActiveProject)
        {
            buttons.Add(InlineKeyboardButton.WithCallbackData("⏳ Expiration", "exp:menu"));
            buttons.Add(InlineKeyboardButton.WithCallbackData("🗑 Remove", "del:menu"));
        }
        return buttons.ToArray();
    }

    /// <summary>
    /// The delete-project confirmation (destructive, so it never fires on a single tap): a
    /// permanent-delete button (<c>del:confirm</c> → the <c>/delete confirm</c> command) and a
    /// Cancel that restores the status menu in place (<c>del:cancel</c>).
    /// </summary>
    public static InlineKeyboardMarkup DeleteConfirmMenu() =>
        new(new[]
        {
            new[] { InlineKeyboardButton.WithCallbackData("🗑 Yes, delete permanently", "del:confirm") },
            new[] { InlineKeyboardButton.WithCallbackData("« Cancel", "del:cancel") },
        });

    /// <summary>The shared top-level rows (Help/Status, Connect/Projects, Create/Save).</summary>
    private static List<InlineKeyboardButton[]> MainRows() =>
        new()
        {
            new[]
            {
                InlineKeyboardButton.WithCallbackData("❓ Help", "help"),
                InlineKeyboardButton.WithCallbackData("ℹ️ Status", "status"),
            },
            new[]
            {
                InlineKeyboardButton.WithCallbackData("🖼 Sources", "sources"),
                InlineKeyboardButton.WithCallbackData("🆕 Blank", "blank"),
            },
            new[]
            {
                InlineKeyboardButton.WithCallbackData("🔌 Connect", "connect"),
                InlineKeyboardButton.WithCallbackData("📁 Projects", "projects"),
            },
            new[]
            {
                InlineKeyboardButton.WithCallbackData("➕ Create", "create"),
                InlineKeyboardButton.WithCallbackData("💾 Save", "save"),
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
                InlineKeyboardButton.WithCallbackData("⏳ 1 day", "exp:1d"),
                InlineKeyboardButton.WithCallbackData("📅 3 days", "exp:3d"),
            },
            new[]
            {
                InlineKeyboardButton.WithCallbackData("🗓 1 week", "exp:1w"),
                InlineKeyboardButton.WithCallbackData("🗓 Fortnight", "exp:2w"),
            },
            new[]
            {
                InlineKeyboardButton.WithCallbackData("🗓 1 month", "exp:1mo"),
                InlineKeyboardButton.WithCallbackData("🗓 3 months", "exp:3mo"),
            },
            new[]
            {
                InlineKeyboardButton.WithCallbackData("✏️ Custom…", "exp:custom"),
                InlineKeyboardButton.WithCallbackData("♾ Never", "exp:never"),
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
                InlineKeyboardButton.WithCallbackData("🎛 Edit", "m:edit"),
                InlineKeyboardButton.WithCallbackData("🎨 Filter", "m:filter"),
                InlineKeyboardButton.WithCallbackData("✏️ Draw", "m:draw"),
            },
            new[]
            {
                InlineKeyboardButton.WithCallbackData("↩️ Undo", "undo"),
                InlineKeyboardButton.WithCallbackData("↪️ Redo", "redo"),
            },
            new[]
            {
                InlineKeyboardButton.WithCallbackData("⬇️ Download", "m:download"),
                InlineKeyboardButton.WithCallbackData("🧼 Reset", "reset"),
                InlineKeyboardButton.WithCallbackData("💾 Save", "save"),
            },
        };
        // The edit menu always rides a rendered image, so Rename is always available here; the
        // server-only Expiration/Remove buttons ride along only when it's a saved server project
        // (the menu shown after /fetch). Same tokens as StatusMenu.
        rows.Add(ProjectActionsRow(hasActiveProject));
        return new InlineKeyboardMarkup(rows);
    }

    /// <summary>Download submenu: rendered image, layout JSON (only when edits exist), whole .stencil project, plus Back.</summary>
    public static InlineKeyboardMarkup DownloadSubmenu(bool hasEdits)
    {
        List<InlineKeyboardButton[]> rows = new()
        {
            new[]
            {
                InlineKeyboardButton.WithCallbackData("🖼 Image", "image"),
                InlineKeyboardButton.WithCallbackData("📦 Project", "project"),
            },
        };
        if (hasEdits)
        {
            rows.Add(new[] { InlineKeyboardButton.WithCallbackData("📄 Layout JSON", "json") });
        }
        rows.Add(BackRow());
        return new InlineKeyboardMarkup(rows);
    }

    /// <summary>Transform submenu: rotate ±90° and crop, plus Back.</summary>
    public static InlineKeyboardMarkup EditSubmenu() =>
        new(new[]
        {
            new[]
            {
                InlineKeyboardButton.WithCallbackData("🔄 Rotate +90°", "rot90"),
                InlineKeyboardButton.WithCallbackData("🔃 Rotate −90°", "rotneg90"),
                InlineKeyboardButton.WithCallbackData("✂️ Crop…", "crophelp"),
            },
            BackRow(),
        });

    /// <summary>Filter submenu: B&amp;W · Sepia · Invert · Contour · custom Tint · None, plus Back.</summary>
    public static InlineKeyboardMarkup FilterSubmenu() =>
        new(new[]
        {
            new[]
            {
                InlineKeyboardButton.WithCallbackData("⚫ B&W", "f:bw"),
                InlineKeyboardButton.WithCallbackData("🟤 Sepia", "f:sepia"),
            },
            new[]
            {
                InlineKeyboardButton.WithCallbackData("🌓 Invert", "f:invert"),
                InlineKeyboardButton.WithCallbackData("〰️ Contour", "f:contour"),
            },
            new[]
            {
                InlineKeyboardButton.WithCallbackData("🎨 Tint…", "tinthelp"),
                InlineKeyboardButton.WithCallbackData("🚫 None", "f:none"),
            },
            BackRow(),
        });

    /// <summary>Draw submenu: how-to, undo last line, clear all lines, plus Back.</summary>
    public static InlineKeyboardMarkup DrawSubmenu() =>
        new(new[]
        {
            new[]
            {
                InlineKeyboardButton.WithCallbackData("✏️ Draw…", "drawhelp"),
                InlineKeyboardButton.WithCallbackData("🩹 Undo line", "undoline"),
                InlineKeyboardButton.WithCallbackData("🧹 Clear lines", "clearlines"),
            },
            BackRow(),
        });

    /// <summary>A single-row "back to the main edit menu" button.</summary>
    private static InlineKeyboardButton[] BackRow() =>
        new[] { InlineKeyboardButton.WithCallbackData("« Back", "m:main") };

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
