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
    public static InlineKeyboardMarkup MainMenu() =>
        new(new[]
        {
            new[]
            {
                InlineKeyboardButton.WithCallbackData("❓ Help", "help"),
                InlineKeyboardButton.WithCallbackData("ℹ️ Status", "status"),
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
        });

    /// <summary>
    /// The main per-result edit menu sent with a rendered image. Transform / Filter / Draw are
    /// group buttons that open a submenu in place (see <see cref="EditSubmenu"/> etc.); the rest
    /// act directly. No "Result" button — the image is already shown and /save persists it.
    /// </summary>
    public static InlineKeyboardMarkup EditMenu() =>
        new(new[]
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
                InlineKeyboardButton.WithCallbackData("📄 JSON", "json"),
                InlineKeyboardButton.WithCallbackData("🧼 Reset", "reset"),
                InlineKeyboardButton.WithCallbackData("💾 Save", "save"),
            },
        });

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

    /// <summary>Filter submenu: B&amp;W · Sepia · custom Tint · None, plus Back.</summary>
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
        foreach (ServerProjectInfo p in projects)
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
