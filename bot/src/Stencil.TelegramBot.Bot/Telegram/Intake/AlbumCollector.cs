using System.Collections.Concurrent;

namespace Stencil.TelegramBot.Bot.Telegram.Intake;

public sealed record AlbumPhoto(int MessageId, string FileId, string? Caption);

// An album arrives as separate messages sharing MediaGroupId; the group flushes after one quiet
// settle window.
public sealed class AlbumCollector
{
    private readonly Func<CancellationToken, Task> _settle;
    private readonly Action? _afterLookup;
    private readonly ConcurrentDictionary<(long UserId, string GroupId), Group> _groups = new();
    private readonly ConcurrentDictionary<Task, byte> _inFlight = new();

    public AlbumCollector(TimeSpan settle)
        : this(ct => Task.Delay(settle, ct))
    {
    }

    public AlbumCollector(Func<CancellationToken, Task> settle)
        : this(settle, null)
    {
    }

    // afterLookup runs between a photo's group lookup and its lock: the window a flush can close.
    public AlbumCollector(Func<CancellationToken, Task> settle, Action? afterLookup)
    {
        _settle = settle;
        _afterLookup = afterLookup;
    }

    // Runs in the background — the caller must NOT hold per-user locks the flush itself acquires.
    public void Add(long userId, string groupId, AlbumPhoto photo,
        Func<IReadOnlyList<AlbumPhoto>, Task> flush, CancellationToken ct)
    {
        (long, string) key = (userId, groupId);
        Group group;
        bool start;
        while (true)
        {
            group = _groups.GetOrAdd(key, _ => new Group());
            _afterLookup?.Invoke();
            lock (group.Lock)
            {
                if (!group.Flushed)
                {
                    group.Photos.Add(photo);
                    group.Generation++;
                    start = !group.Started;
                    group.Started = true;
                    break;
                }
            }
            // That group already flushed: retire it if its flusher has not yet, and start a new one.
            _groups.TryRemove(new KeyValuePair<(long, string), Group>(key, group));
        }
        if (start)
        {
            Task task = flushWhenSettledAsync(key, group, flush, ct);
            _inFlight.TryAdd(task, 0);
            _ = task.ContinueWith(t => _inFlight.TryRemove(t, out _), TaskScheduler.Default);
        }
    }

    public Task WhenIdleAsync() => Task.WhenAll(_inFlight.Keys);

    private async Task flushWhenSettledAsync((long, string) key, Group group,
        Func<IReadOnlyList<AlbumPhoto>, Task> flush, CancellationToken ct)
    {
        List<AlbumPhoto> photos;
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
                        group.Flushed = true;
                        photos = new List<AlbumPhoto>(group.Photos);
                        break;
                    }
                }
            }
        }
        catch (OperationCanceledException)
        {
            // Shutdown — drop the buffered group quietly.
            lock (group.Lock)
            {
                group.Flushed = true;
            }
            _groups.TryRemove(new KeyValuePair<(long, string), Group>(key, group));
            return;
        }
        _groups.TryRemove(new KeyValuePair<(long, string), Group>(key, group));
        await flush(photos).ConfigureAwait(false);
    }

    private sealed class Group
    {
        public readonly object Lock = new();
        public readonly List<AlbumPhoto> Photos = new();
        public int Generation;
        public bool Started;
        public bool Flushed; // set under Lock once the photos are taken; a later Add must not join
    }
}
