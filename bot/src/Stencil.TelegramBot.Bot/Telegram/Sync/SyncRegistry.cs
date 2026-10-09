using System.Collections.Concurrent;

namespace Stencil.TelegramBot.Bot.Telegram.Sync;

// Thread-safe: the handlers and the background poller both touch it.
public sealed class SyncRegistry
{
    private readonly ConcurrentDictionary<long, long> _chatByUser = new();
    private readonly ConcurrentDictionary<long, byte> _consulted = new();

    // Retargets when already enabled.
    public void Enable(long userId, long chatId) => _chatByUser[userId] = chatId;

    // Never retargets: a restart forgets the map, and the user's next update re-adds the chat /sync named.
    public void Rejoin(long userId, long chatId) => _chatByUser.TryAdd(userId, chatId);

    // A user's stored SyncEnabled is read once per process; Enable and Disable keep the map true after.
    public bool Consulted(long userId) => _consulted.ContainsKey(userId);

    public void MarkConsulted(long userId) => _consulted.TryAdd(userId, 0);

    public void Disable(long userId) => _chatByUser.TryRemove(userId, out _);

    // A snapshot, so the poller iterates without holding the map.
    public IReadOnlyList<(long UserId, long ChatId)> Entries() =>
        _chatByUser.Select(kv => (kv.Key, kv.Value)).ToList();
}
