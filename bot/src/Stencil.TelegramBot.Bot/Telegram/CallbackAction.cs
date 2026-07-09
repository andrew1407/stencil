using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Telegram.Bot.Types;
using Telegram.Bot.Types.ReplyMarkups;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Translates an inline-button callback payload into the matching command. Every menu button
/// rides a short token (see <see cref="Keyboards"/>); this maps the token back to a synthetic
/// <see cref="BotCommand"/> and runs it through <see cref="CommandHandlers.DispatchAsync"/>, so
/// a tap and the equivalent slash command share one code path. Arg-requiring buttons that can't
/// act on their own (e.g. Connect) just reply with the slash command to use instead.
/// </summary>
public sealed class CallbackAction
{
    private readonly CommandHandlers _handlers;
    private readonly ITelegramBotClient _bot;
    private readonly ISessionStore _store;

    public CallbackAction(CommandHandlers handlers, ITelegramBotClient bot, ISessionStore store)
    {
        _handlers = handlers;
        _bot = bot;
        _store = store;
    }

    /// <summary>
    /// Acknowledge the callback, then dispatch its token. The owning user is the tapper; the
    /// chat is the message the keyboard is attached to.
    /// </summary>
    public async Task HandleAsync(CallbackQuery query, CancellationToken ct)
    {
        await _bot.AnswerCallbackQuery(query.Id, cancellationToken: ct);
        if (query.Message is null)
        {
            return;
        }
        long userId = query.From.Id;
        long chatId = query.Message.Chat.Id;
        string data = query.Data ?? "";
        // Arg-requiring or help-only buttons can't act on their own — reply with guidance.
        if (data == "connect")
        {
            await _bot.SendMessage(chatId, Replies.ConnectUsage(), cancellationToken: ct);
            return;
        }
        if (data == "drawhelp")
        {
            await _bot.SendMessage(chatId, Replies.DrawHelp(), cancellationToken: ct);
            return;
        }
        if (data == "sources")
        {
            await _bot.SendMessage(chatId, Replies.SourcesHelp(), cancellationToken: ct);
            return;
        }
        if (data == "crophelp")
        {
            await _bot.SendMessage(chatId, Replies.CropUsage(), cancellationToken: ct);
            return;
        }
        if (data == "tinthelp")
        {
            await _bot.SendMessage(chatId, "Tint with a custom colour: /filter <colour>, e.g. /filter #ff5623 or /filter teal. (B&W, Sepia, Invert and Contour have their own buttons; None clears it.)", cancellationToken: ct);
            return;
        }
        // Group buttons swap the inline keyboard in place (submenu navigation), no edit performed.
        // Returning to the main edit menu re-reads the session so the project-actions row (shown
        // only for a server project) is restored after a submenu detour.
        if (data.StartsWith("m:", StringComparison.Ordinal))
        {
            InlineKeyboardMarkup markup = data switch
            {
                "m:edit" => Keyboards.EditSubmenu(),
                "m:filter" => Keyboards.FilterSubmenu(),
                "m:draw" => Keyboards.DrawSubmenu(),
                _ => Keyboards.EditMenu(await HasActiveProjectAsync(userId, ct)),
            };
            await _bot.EditMessageReplyMarkup(chatId, query.Message.MessageId, markup, cancellationToken: ct);
            return;
        }
        // Cancel on the delete confirmation just retires the prompt (the confirmation is its own
        // message; the destructive del:confirm falls through to the /delete command below).
        if (data == "del:cancel")
        {
            await _bot.EditMessageText(chatId, query.Message.MessageId, "Removal cancelled.", cancellationToken: ct);
            return;
        }
        await _handlers.DispatchAsync(userId, chatId, Map(data), ct);
    }

    /// <summary>Map a callback token to the equivalent slash command.</summary>
    private static BotCommand Map(string data)
    {
        if (data.StartsWith("fetch:", StringComparison.Ordinal))
        {
            string id = data["fetch:".Length..];
            return new BotCommand("fetch", id, [id]);
        }
        return data switch
        {
            "rot90" => new BotCommand("rotate", "1", ["1"]),
            "rotneg90" => new BotCommand("rotate", "-1", ["-1"]),
            "f:bw" => new BotCommand("filter", "bw", ["bw"]),
            "f:sepia" => new BotCommand("filter", "sepia", ["sepia"]),
            "f:invert" => new BotCommand("filter", "invert", ["invert"]),
            "f:contour" => new BotCommand("filter", "contour", ["contour"]),
            "f:none" => new BotCommand("filter", "none", ["none"]),
            // The Expiration / Remove entry buttons dispatch the bare command, which replies with
            // the duration picker / delete confirmation as a fresh message (so the same button
            // works from both the status menu and the image edit menu).
            "exp:menu" => new BotCommand("expire", "", []),
            "del:menu" => new BotCommand("delete", "", []),
            // Expiry presets ride the equivalent /expire command; "custom" opens the free-text
            // prompt and "never" clears the expiry — both handled inside the /expire handler.
            "exp:1d" => new BotCommand("expire", "1 day", ["1", "day"]),
            "exp:3d" => new BotCommand("expire", "3 days", ["3", "days"]),
            "exp:1w" => new BotCommand("expire", "1 week", ["1", "week"]),
            "exp:2w" => new BotCommand("expire", "fortnight", ["fortnight"]),
            "exp:1mo" => new BotCommand("expire", "1 month", ["1", "month"]),
            "exp:3mo" => new BotCommand("expire", "3 months", ["3", "months"]),
            "exp:custom" => new BotCommand("expire", "custom", ["custom"]),
            "exp:never" => new BotCommand("expire", "never", ["never"]),
            // Delete's actual removal only runs from the explicit confirm button; the menu/cancel
            // tokens are handled above (dispatch the confirmation / retire it).
            "del:confirm" => new BotCommand("delete", "confirm", ["confirm"]),
            // All other tokens are bare verbs (help/projects/create/save/status/image/json/
            // reset/undoline/clearlines), dispatched as-is.
            _ => new BotCommand(data, "", []),
        };
    }

    /// <summary>Whether the tapper currently has a server project open (drives the project-actions row).</summary>
    private async Task<bool> HasActiveProjectAsync(long userId, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        return session.ActiveProjectId is not null;
    }
}
