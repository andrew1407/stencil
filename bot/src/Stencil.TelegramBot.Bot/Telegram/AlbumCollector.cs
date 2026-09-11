using System.Collections.Concurrent;

namespace Stencil.TelegramBot.Bot.Telegram;

public sealed record AlbumPhoto(int MessageId, string FileId, string? Caption);

/// <summary>
/// Buffers a Telegram media group's photos — an album arrives as separate messages sharing
/// <c>MediaGroupId</c>, with the caption on one of them — and flushes the whole group in the
/// background once no new member has arrived for one settle window. The wait is injectable so
/// offline tests never sleep.
/// </summary>
public sealed class AlbumCollector
{
    /// <summary>Album members usually land within ~a second; flush after this much quiet.</summary>
    private static readonly TimeSpan DefaultSettle = TimeSpan.FromSeconds(1);

    private readonly Func<CancellationToken, Task> _settle;
    private readonly ConcurrentDictionary<(long UserId, string GroupId), Group> _groups = new();
    private readonly ConcurrentDictionary<Task, byte> _inFlight = new();

    public AlbumCollector(Func<CancellationToken, Task>? settle = null)
    {
        _settle = settle ?? (ct => Task.Delay(DefaultSettle, ct));
    }

    /// <summary>
    /// Buffer one album member; the group's first member starts the settle-and-flush task, which
    /// hands the full group to <paramref name="flush"/> once it stops growing. Runs in the
    /// background — the caller must NOT hold per-user locks the flush itself acquires.
    /// </summary>
    public void Add(long userId, string groupId, AlbumPhoto photo,
        Func<IReadOnlyList<AlbumPhoto>, Task> flush, CancellationToken ct)
    {
        (long, string) key = (userId, groupId);
        Group group = _groups.GetOrAdd(key, _ => new Group());
        bool start;
        lock (group.Lock)
        {
            group.Photos.Add(photo);
            group.Generation++;
            start = !group.Started;
            group.Started = true;
        }
        if (start)
        {
            Task task = FlushWhenSettledAsync(key, group, flush, ct);
            _inFlight.TryAdd(task, 0);
            _ = task.ContinueWith(t => _inFlight.TryRemove(t, out _), TaskScheduler.Default);
        }
    }

    /// <summary>Completes when every started group has flushed — lets tests await, not sleep.</summary>
    public Task WhenIdleAsync() => Task.WhenAll(_inFlight.Keys);

    private async Task FlushWhenSettledAsync((long, string) key, Group group,
        Func<IReadOnlyList<AlbumPhoto>, Task> flush, CancellationToken ct)
    {
        try
        {
            while (true)
            {
                int seen;
                lock (group.Lock)
                {
                    seen = group.Generation;
                }
                await _settle(ct).ConfigureAwait(false);
                lock (group.Lock)
                {
                    if (group.Generation == seen)
                    {
                        break;
                    }
                }
            }
        }
        catch (OperationCanceledException)
        {
            // Shutdown — drop the buffered group quietly.
            _groups.TryRemove(key, out _);
            return;
        }
        _groups.TryRemove(key, out _);
        List<AlbumPhoto> photos;
        lock (group.Lock)
        {
            photos = new List<AlbumPhoto>(group.Photos);
        }
        await flush(photos).ConfigureAwait(false);
    }

    private sealed class Group
    {
        public readonly object Lock = new();
        public readonly List<AlbumPhoto> Photos = new();
        public int Generation;
        public bool Started;
    }
}
