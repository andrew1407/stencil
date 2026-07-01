using System.Collections.Concurrent;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// The set of users with live sync enabled, mapping each to the chat their updates go to.
/// Populated by <c>/sync on</c> and drained by <c>/sync off</c> (or /drop); the
/// <see cref="SyncWatcher"/> polls exactly this set. Thread-safe (touched by handlers and the
/// background poller).
/// </summary>
public sealed class SyncRegistry
{
    private readonly ConcurrentDictionary<long, long> _chatByUser = new();

    /// <summary>Start (or retarget) live sync for a user, delivering pulls to <paramref name="chatId"/>.</summary>
    public void Enable(long userId, long chatId) => _chatByUser[userId] = chatId;

    /// <summary>Stop live sync for a user.</summary>
    public void Disable(long userId) => _chatByUser.TryRemove(userId, out _);

    /// <summary>A snapshot of the current (userId, chatId) pairs to poll.</summary>
    public IReadOnlyList<(long UserId, long ChatId)> Entries() =>
        _chatByUser.Select(kv => (kv.Key, kv.Value)).ToList();
}
