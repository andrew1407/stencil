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

    public CallbackAction(CommandHandlers handlers, ITelegramBotClient bot)
    {
        _handlers = handlers;
        _bot = bot;
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
            await _bot.SendMessage(chatId, "Use /connect <url> [token] to connect to a server.", cancellationToken: ct);
            return;
        }
        if (data == "drawhelp")
        {
            await _bot.SendMessage(chatId, Replies.DrawHelp(), cancellationToken: ct);
            return;
        }
        if (data == "crophelp")
        {
            await _bot.SendMessage(chatId, "Crop with /crop <spec>, e.g. /crop x1=10% x2=90% y1=10% y2=90% (add 'album' to derive the missing axis).", cancellationToken: ct);
            return;
        }
        if (data == "tinthelp")
        {
            await _bot.SendMessage(chatId, "Tint with a custom colour: /filter <colour>, e.g. /filter #ff5623 or /filter teal. (B&W and Sepia have their own buttons; None clears it.)", cancellationToken: ct);
            return;
        }
        // Group buttons swap the inline keyboard in place (submenu navigation), no edit performed.
        if (data.StartsWith("m:", StringComparison.Ordinal))
        {
            InlineKeyboardMarkup markup = data switch
            {
                "m:edit" => Keyboards.EditSubmenu(),
                "m:filter" => Keyboards.FilterSubmenu(),
                "m:draw" => Keyboards.DrawSubmenu(),
                _ => Keyboards.EditMenu(),
            };
            await _bot.EditMessageReplyMarkup(chatId, query.Message.MessageId, markup, cancellationToken: ct);
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
            "f:none" => new BotCommand("filter", "none", ["none"]),
            // All other tokens are bare verbs (help/projects/create/save/status/image/json/
            // reset/undoline/clearlines), dispatched as-is.
            _ => new BotCommand(data, "", []),
        };
    }
}
