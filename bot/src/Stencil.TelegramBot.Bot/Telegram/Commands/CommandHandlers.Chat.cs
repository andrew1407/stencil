using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;
using Stencil.TelegramBot.Bot.Telegram.Messaging;

namespace Stencil.TelegramBot.Bot.Telegram.Commands;

public sealed partial class CommandHandlers
{
    // The list is the operator's; a user picks and never types an endpoint (see LlmProfile). Per
    // user, in the session.
    private async Task chatApiAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        if (_options.LlmProfiles.Count == 0)
        {
            await _bot.SendMessage(chatId, Replies.ChatApiNoProfiles(), cancellationToken: ct);
            return;
        }
        UserSession session = await _store.GetAsync(userId, ct);
        string wanted = cmd.ArgumentText.Trim();
        if (wanted.Length == 0)
        {
            await _bot.SendMessage(
                chatId,
                Replies.ChatApiList(_options.LlmProfiles, _options.FindProfile(session.LlmProfile)),
                replyMarkup: Keyboards.ChatApiMenu(_options.LlmProfiles, session.LlmProfile),
                cancellationToken: ct);
            return;
        }
        if (_options.FindProfile(wanted) is not LlmProfile picked)
        {
            await _bot.SendMessage(chatId, Replies.ChatApiUnknown(wanted, _options.LlmProfiles), cancellationToken: ct);
            return;
        }
        await SelectChatApiAsync(userId, chatId, picked, ct);
    }

    // A card can outlive the configuration it was drawn from.
    public async Task SelectChatApiOrExplainAsync(long userId, long chatId, string name, CancellationToken ct)
    {
        if (_options.FindProfile(name) is not LlmProfile picked)
        {
            await _bot.SendMessage(chatId, Replies.ChatApiUnknown(name, _options.LlmProfiles), cancellationToken: ct);
            return;
        }
        await SelectChatApiAsync(userId, chatId, picked, ct);
    }

    public async Task SelectChatApiAsync(long userId, long chatId, LlmProfile picked, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        await _store.SaveAsync(session with { LlmProfile = picked.Name }, ct);
        await _bot.SendMessage(
            chatId,
            Replies.ChatApiSelected(picked),
            replyMarkup: Keyboards.ChatApiMenu(_options.LlmProfiles, picked.Name),
            cancellationToken: ct);
    }

    // While on, UpdateRouter hands each unclaimed plain message to PromptAsync — chatting and
    // /prompt are one path.
    private async Task chatAsync(long userId, long chatId, BotCommand cmd, CancellationToken ct)
    {
        string arg = cmd.ArgumentText.Trim().ToLowerInvariant();
        if (arg is "clear" or "reset" or "forget" or "new")
        {
            // Never gated: someone who used the assistant before the allowlist tightened must still
            // be able to delete it.
            await clearChatHistoryAsync(userId, chatId, ct);
            return;
        }
        if (arg == "save" || arg.StartsWith("save ", StringComparison.Ordinal))
        {
            await chatSaveAsync(userId, chatId, arg["save".Length..].Trim(), ct);
            return;
        }
        bool? enable = arg switch
        {
            "" or "on" or "start" => true,
            "off" or "stop" or "end" or "exit" => false,
            _ => null,
        };
        if (enable is not bool on)
        {
            await _bot.SendMessage(chatId, Replies.ChatUsage(), cancellationToken: ct);
            return;
        }
        UserSession session = await _store.GetAsync(userId, ct);
        if (session.ChatMode != on)
        {
            await _store.SaveAsync(session with { ChatMode = on }, ct);
        }
        if (on)
        {
            await _bot.SendMessage(chatId, Replies.ChatModeOn(), replyMarkup: Keyboards.ChatModeMenu(session.SaveChats), cancellationToken: ct);
        }
        else
        {
            await _bot.SendMessage(chatId, Replies.ChatModeOff(), cancellationToken: ct);
        }
    }

    // §12.3, default OFF; the store is the active SERVER project's chat file. Off stops writing,
    // never deletes.
    private async Task chatSaveAsync(long userId, long chatId, string arg, CancellationToken ct)
    {
        UserSession session = await _store.GetAsync(userId, ct);
        if (arg.Length == 0)
        {
            await _bot.SendMessage(
                chatId,
                Replies.ChatSaveStatus(session.SaveChats),
                replyMarkup: session.ChatMode ? Keyboards.ChatModeMenu(session.SaveChats) : null,
                cancellationToken: ct);
            return;
        }
        bool? enable = arg switch
        {
            "on" or "true" or "1" or "yes" => true,
            "off" or "false" or "0" or "no" => false,
            _ => null,
        };
        if (enable is not bool on)
        {
            await _bot.SendMessage(chatId, Replies.ChatUsage(), cancellationToken: ct);
            return;
        }
        if (session.SaveChats != on)
        {
            await _store.SaveAsync(session with { SaveChats = on }, ct);
        }
        await _bot.SendMessage(
            chatId,
            on ? Replies.ChatSaveOn() : Replies.ChatSaveOff(),
            replyMarkup: session.ChatMode ? Keyboards.ChatModeMenu(on) : null,
            cancellationToken: ct);
    }

    // Chat mode, image and edits are untouched; per §12.2 the persisted server copy goes too
    // (best-effort).
    private async Task clearChatHistoryAsync(long userId, long chatId, CancellationToken ct)
    {
        _prompts.ClearHistory(userId);
        UserSession session = await _store.GetAsync(userId, ct);
        if (session.SaveChats && session.ActiveProjectId is not null)
        {
            try
            {
                await _servers.DeleteChatAsync(userId, ct);
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                _logger.LogWarning(ex, "Server chat delete failed for user {UserId}", userId);
            }
        }
        await _bot.SendMessage(
            chatId,
            Replies.ChatHistoryCleared(session.ChatMode),
            replyMarkup: session.ChatMode ? Keyboards.ChatModeMenu(session.SaveChats) : null,
            cancellationToken: ct);
    }

}
