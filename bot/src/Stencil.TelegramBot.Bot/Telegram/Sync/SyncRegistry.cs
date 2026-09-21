using System.Collections.Concurrent;

namespace Stencil.TelegramBot.Bot.Telegram.Sync;

// Thread-safe: the handlers and the background poller both touch it.
public sealed class SyncRegistry
{
    private readonly ConcurrentDictionary<long, long> _chatByUser = new();

    // Retargets when already enabled.
    public void Enable(long userId, long chatId) => _chatByUser[userId] = chatId;

    public void Disable(long userId) => _chatByUser.TryRemove(userId, out _);

    // A snapshot, so the poller iterates without holding the map.
    public IReadOnlyList<(long UserId, long ChatId)> Entries() =>
        _chatByUser.Select(kv => (kv.Key, kv.Value)).ToList();
}
