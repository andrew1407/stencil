using System.Collections.Concurrent;

namespace Stencil.TelegramBot.Bot.Telegram;

public sealed record AlbumPhoto(int MessageId, string FileId, string? Caption);

// An album arrives as separate messages sharing MediaGroupId; the group flushes after one quiet
// settle window.
public sealed class AlbumCollector
{
    // Album members usually land within ~a second.
    private static readonly TimeSpan DefaultSettle = TimeSpan.FromSeconds(1);

    private readonly Func<CancellationToken, Task> _settle;
    private readonly ConcurrentDictionary<(long UserId, string GroupId), Group> _groups = new();
    private readonly ConcurrentDictionary<Task, byte> _inFlight = new();

    public AlbumCollector(Func<CancellationToken, Task>? settle = null)
    {
        _settle = settle ?? (ct => Task.Delay(DefaultSettle, ct));
    }

    // Runs in the background — the caller must NOT hold per-user locks the flush itself acquires.
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
            Task task = flushWhenSettledAsync(key, group, flush, ct);
            _inFlight.TryAdd(task, 0);
            _ = task.ContinueWith(t => _inFlight.TryRemove(t, out _), TaskScheduler.Default);
        }
    }

    public Task WhenIdleAsync() => Task.WhenAll(_inFlight.Keys);

    private async Task flushWhenSettledAsync((long, string) key, Group group,
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
