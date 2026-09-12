using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;

namespace Stencil.TelegramBot.Bot.Telegram;

public sealed partial class CommandHandlers
{
    // A failure never fails the turn: logged, and warned once (ChatSaveWarned, re-armed by the next
    // success).
    private async Task persistChatAsync(long userId, long chatId, UserSession session, CancellationToken ct)
    {
        if (!session.SaveChats || session.ActiveProjectId is null)
        {
            return;
        }
        if (_prompts.BuildChatDocument(userId) is not ChatDocument doc || doc.Messages.Count == 0)
        {
            return;
        }
        try
        {
            await _servers.SaveChatAsync(userId, doc.ToJson(), ct);
            if (session.ChatSaveWarned)
            {
                await setChatSaveWarnedAsync(userId, false, ct);
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            _logger.LogWarning(ex, "Chat save-back to the server failed for user {UserId}", userId);
            if (!session.ChatSaveWarned)
            {
                await setChatSaveWarnedAsync(userId, true, ct);
                await _bot.SendMessage(chatId, Replies.ChatSaveFailed(), cancellationToken: ct);
            }
        }
    }

    // Re-fetches before saving: the ask card sent earlier in the turn may have stored state this
    // copy predates.
    private async Task setChatSaveWarnedAsync(long userId, bool warned, CancellationToken ct)
    {
        UserSession latest = await _store.GetAsync(userId, ct);
        await _store.SaveAsync(latest with { ChatSaveWarned = warned }, ct);
    }



    // Only with chat saving on; best-effort. Returns the number of restored messages.
    private async Task<int> tryRestoreChatAsync(long userId, UserSession session, CancellationToken ct)
    {
        if (!session.SaveChats || session.ActiveProjectId is null)
        {
            return 0;
        }
        try
        {
            string? json = await _servers.LoadChatAsync(userId, ct);
            if (json is null || ChatDocument.TryParse(json) is not ChatDocument doc || doc.Messages.Count == 0)
            {
                return 0;
            }
            return _prompts.SeedHistory(userId, doc);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            _logger.LogWarning(ex, "Chat restore from the server failed for user {UserId}", userId);
            return 0;
        }
    }
}
