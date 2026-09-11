using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Domain.Sessions;
using Telegram.Bot;

namespace Stencil.TelegramBot.Bot.Telegram;

// CommandHandlers — the §12 chat document: pushing it to the active server project and seeding
// the assistant's history back from it. Class doc lives in CommandHandlers.cs.
public sealed partial class CommandHandlers
{
    /// <summary>
    /// Push the §12.1 chat document to the active server project when chat saving is on. A
    /// failure never fails the turn: it is logged and surfaced as a short warning only once
    /// (<see cref="UserSession.ChatSaveWarned"/>, re-armed by the next successful save).
    /// </summary>
    private async Task PersistChatAsync(long userId, long chatId, UserSession session, CancellationToken ct)
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
                await SetChatSaveWarnedAsync(userId, false, ct);
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            _logger.LogWarning(ex, "Chat save-back to the server failed for user {UserId}", userId);
            if (!session.ChatSaveWarned)
            {
                await SetChatSaveWarnedAsync(userId, true, ct);
                await _bot.SendMessage(chatId, Replies.ChatSaveFailed(), cancellationToken: ct);
            }
        }
    }

    /// <summary>
    /// Flip the persisted once-only chat-save warning flag. Re-fetches before saving: the ask
    /// card sent earlier in the same turn may have stored state the caller's copy predates.
    /// </summary>
    private async Task SetChatSaveWarnedAsync(long userId, bool warned, CancellationToken ct)
    {
        UserSession latest = await _store.GetAsync(userId, ct);
        await _store.SaveAsync(latest with { ChatSaveWarned = warned }, ct);
    }



    /// <summary>
    /// Pull the fetched project's persisted chat (§12.1) and seed the assistant's history from
    /// it. Only with chat saving on; best-effort (a missing/invalid document or an unreachable
    /// file route restores nothing). Returns the number of restored messages.
    /// </summary>
    private async Task<int> TryRestoreChatAsync(long userId, UserSession session, CancellationToken ct)
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
