using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Domain.Llm;
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
    /// <summary>
    /// The §11 choice card: one button per option, plus a Send button when the card takes
    /// several picks. A ticked option shows ✓ — the selection and the labels live in the
    /// session (see <c>UserSession.AskOptions</c> for why callback data cannot carry them).
    /// Tapping never applies an edit; it only composes the answer sent as the user's next turn.
    /// </summary>
    public static InlineKeyboardMarkup AskCardKeyboard(IReadOnlyList<string> options, bool multi, IReadOnlyList<int> picked, bool allowCustom)
    {
        List<InlineKeyboardButton[]> rows = new();
        for (int i = 0; i < options.Count; i++)
        {
            string tick = multi && picked.Contains(i) ? "☑️ " : multi ? "⬜️ " : "";
            rows.Add([InlineKeyboardButton.WithCallbackData($"{tick}{Trim(options[i])}", $"ask:{i}")]);
        }
        if (multi)
        {
            rows.Add([InlineKeyboardButton.WithCallbackData("✅ Send", "ask:send")]);
        }
        if (allowCustom)
        {
            // No button: a custom answer IS just typing, which chat mode already forwards.
            rows.Add([InlineKeyboardButton.WithCallbackData("✍️ Type my own", "ask:custom")]);
        }
        return new InlineKeyboardMarkup(rows);
    }

    /// <summary>
    /// The one-button keyboard on an assistant turn that did not deliver — it failed, or the user
    /// stopped it. Re-runs the same prompt (its text is held in
    /// <c>UserSession.LastRetryablePrompt</c>, since callback data cannot carry it). A timed-out or
    /// unreachable endpoint is the common case, and retyping the prompt on a phone is the worst way
    /// to recover from it.
    /// </summary>
    public static InlineKeyboardMarkup RetryPrompt() =>
        new(new[] { new[] { InlineKeyboardButton.WithCallbackData("🔄 Retry", "retry:prompt") } });

    /// <summary>
    /// The one-button keyboard on the assistant's working notice. A turn can run for minutes and
    /// holds the user's gate while it does, so without this there is no way to call it off —
    /// see <see cref="PromptCancellations"/> for why the tap bypasses that gate.
    /// </summary>
    public static InlineKeyboardMarkup StopPrompt() =>
        new(new[] { new[] { InlineKeyboardButton.WithCallbackData("⏹ Stop", "stop:prompt") } });

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
            string tick = string.Equals(p.Name, current, StringComparison.OrdinalIgnoreCase) ? "✅ " : "";
            rows.Add([InlineKeyboardButton.WithCallbackData($"{tick}{Trim(p.Label)}", $"api:{p.Name}")]);
        }
        return new InlineKeyboardMarkup(rows);
    }

    /// <summary>Button labels have to stay readable on a phone — long option text is clipped.</summary>
    private static string Trim(string label) => label.Length <= 40 ? label : label[..39] + "…";

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
            rows.AddRange(ProjectActionsRows(hasActiveProject: true));
        }
        return new InlineKeyboardMarkup(rows);
    }

    /// <summary>
    /// The project-actions rows shared by <see cref="StatusMenu"/> and <see cref="EditMenu"/>.
    /// Rename and Describe are always offered (they also apply to a not-yet-saved working image,
    /// carried into <c>/create</c>); Link, Expiration and Remove need a saved server project, so
    /// they ride a second row only when <paramref name="hasActiveProject"/> — five buttons abreast
    /// squeeze their labels away on a phone. Each token dispatches the equivalent command (which
    /// replies with the link / prompt / picker / confirmation as a fresh message), so they work
    /// identically from either menu.
    /// </summary>
    private static List<InlineKeyboardButton[]> ProjectActionsRows(bool hasActiveProject)
    {
        List<InlineKeyboardButton[]> rows = new()
        {
            new[]
            {
                InlineKeyboardButton.WithCallbackData("✏️ Rename", "name:menu"),
                InlineKeyboardButton.WithCallbackData("📝 Describe", "desc:menu"),
            },
        };
        if (hasActiveProject)
        {
            rows.Add(new[]
            {
                // Bare verb token: CallbackAction dispatches it as /link.
                InlineKeyboardButton.WithCallbackData("🔗 Link", "link"),
                InlineKeyboardButton.WithCallbackData("⏳ Expiration", "exp:menu"),
                InlineKeyboardButton.WithCallbackData("🗑 Remove", "del:menu"),
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
            new[] { InlineKeyboardButton.WithCallbackData("🗑 Yes, delete permanently", "del:confirm") },
            new[] { InlineKeyboardButton.WithCallbackData("« Cancel", "del:cancel") },
        });

    /// <summary>
    /// The §10 <c>clearChat</c> confirmation (the clear never fires on the model's word alone):
    /// Yes rides the same <c>/chat clear</c> path as the 🧹 button (<c>chatclear:confirm</c>);
    /// Cancel retires the prompt with a "clear canceled" note (<c>chatclear:cancel</c>).
    /// </summary>
    public static InlineKeyboardMarkup ClearChatConfirmMenu() =>
        new(new[]
        {
            new[] { InlineKeyboardButton.WithCallbackData("🧹 Yes, clear it", "chatclear:confirm") },
            new[] { InlineKeyboardButton.WithCallbackData("« Cancel", "chatclear:cancel") },
        });

    /// <summary>
    /// The chat-mode keyboard sent with the chat-mode confirmation (and with a clear): the visible
    /// way back out of chat mode (token <c>chat:off</c>) plus "forget the conversation"
    /// (<c>chat:clear</c>) — the same two things <c>/chat off</c> and <c>/chat clear</c> do — and
    /// the §12 chat-persistence toggle, whose label shows the current setting and whose token
    /// (<c>chat:save-on</c>/<c>chat:save-off</c>) flips it like <c>/chat save on|off</c> would.
    /// </summary>
    public static InlineKeyboardMarkup ChatModeMenu(bool saveChats = false) =>
        new(new[]
        {
            new[]
            {
                InlineKeyboardButton.WithCallbackData("🧹 Clear chat", "chat:clear"),
                InlineKeyboardButton.WithCallbackData("🚪 Chat off", "chat:off"),
            },
            new[]
            {
                InlineKeyboardButton.WithCallbackData(
                    saveChats ? "💾 Save chats: on" : "💾 Save chats: off",
                    saveChats ? "chat:save-off" : "chat:save-on"),
            },
        });

    /// <summary>The shared top-level rows (Chat, Help/Status, Connect/Projects, Create/Save).</summary>
    private static List<InlineKeyboardButton[]> MainRows() =>
        new()
        {
            // Chat mode gets its own full-width row: it's the entry point to the assistant, and
            // the token is the same command (/chat on) the slash surface exposes.
            new[]
            {
                InlineKeyboardButton.WithCallbackData("💬 Chat with assistant", "chat:on"),
            },
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
                InlineKeyboardButton.WithCallbackData("💬 Chat", "chat:on"),
            },
            new[]
            {
                InlineKeyboardButton.WithCallbackData("⬇️ Download", "m:download"),
                InlineKeyboardButton.WithCallbackData("🧼 Reset", "reset"),
                InlineKeyboardButton.WithCallbackData("💾 Save", "save"),
            },
        };
        // The edit menu always rides a rendered image, so Rename is always available here; the
        // server-only Link/Expiration/Remove row rides along only when it's a saved server project
        // (the menu shown after /fetch). Same tokens as StatusMenu.
        rows.AddRange(ProjectActionsRows(hasActiveProject));
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
