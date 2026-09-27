using System.Threading.Channels;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Infrastructure.Configuration;

namespace Stencil.TelegramBot.Bot;

// Telegram.Bot awaits each handler before the next update and an assistant turn runs for minutes, so
// the pump detaches each update onto a worker pool. One serial lane per user: a busy user's updates wait
// in their lane without holding a worker, and ready lanes take turns.
public sealed class UpdatePump : IAsyncDisposable
{
    // A lane is listed at most once: while it waits here or while a worker runs its head.
    private readonly Channel<long> _ready = Channel.CreateUnbounded<long>();
    private readonly Dictionary<long, Queue<Func<Task>>> _lanes = [];
    // One slot per queued or running update; a full pump holds the poller back.
    private readonly SemaphoreSlim _room;
    private readonly TaskCompletionSource _idle = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly int _perLane;
    private readonly TimeSpan _drainTimeout;
    private readonly ILogger _logger;
    private int _pending;
    private bool _closing;

    public UpdatePump(ILogger logger, BotOptions options)
    {
        _logger = logger;
        _room = new SemaphoreSlim(options.UpdateQueueCapacity, options.UpdateQueueCapacity);
        _perLane = options.MaxPendingPerUser;
        _drainTimeout = options.ShutdownDrainTimeout;
        for (int i = 0; i < options.UpdateWorkers; i++)
        {
            _ = Task.Run(workAsync);
        }
    }

    // A null lane runs at once, off every lane: the Stop tap, whose turn is at the head of its own.
    // False when the update was dropped: its lane already holds MaxPendingPerUser, or the pump is closing.
    public async Task<bool> EnqueueAsync(long? lane, Func<Task> work)
    {
        await _room.WaitAsync().ConfigureAwait(false);
        lock (_lanes)
        {
            Queue<Func<Task>>? queue = null;
            bool full = lane is long key && _lanes.TryGetValue(key, out queue) && queue.Count >= _perLane;
            if (_closing || full)
            {
                _room.Release();
                _logger.LogWarning("Dropped an update for {Lane}: its lane is full or the pump is closing", lane);
                return false;
            }
            _pending++;
            if (lane is not long id)
            {
                _ = Task.Run(() => runUrgentAsync(work));
            }
            else if (queue is not null)
            {
                queue.Enqueue(work);
            }
            else
            {
                queue = new Queue<Func<Task>>();
                queue.Enqueue(work);
                _lanes[id] = queue;
                _ready.Writer.TryWrite(id);
            }
        }
        return true;
    }

    public async ValueTask DisposeAsync()
    {
        lock (_lanes)
        {
            _closing = true;
            if (_pending == 0)
            {
                _idle.TrySetResult();
            }
        }
        // Bounded: shutdown already cancelled the handlers, so none can hold the process open.
        await Task.WhenAny(_idle.Task, Task.Delay(_drainTimeout)).ConfigureAwait(false);
        _ready.Writer.TryComplete();
    }

    private async Task workAsync()
    {
        await foreach (long key in _ready.Reader.ReadAllAsync())
        {
            Func<Task> work;
            lock (_lanes)
            {
                work = _lanes[key].Dequeue();
            }
            await runAsync(work).ConfigureAwait(false);
            lock (_lanes)
            {
                if (_lanes[key].Count == 0)
                {
                    _lanes.Remove(key);
                }
                else
                {
                    _ready.Writer.TryWrite(key); // behind every lane already waiting
                }
            }
            finished();
        }
    }

    private async Task runUrgentAsync(Func<Task> work)
    {
        await runAsync(work).ConfigureAwait(false);
        finished();
    }

    private void finished()
    {
        _room.Release();
        lock (_lanes)
        {
            if (--_pending == 0 && _closing)
            {
                _idle.TrySetResult();
            }
        }
    }

    private async Task runAsync(Func<Task> work)
    {
        try
        {
            await work().ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            // Shutdown — the router's guard lets these through on purpose.
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Unhandled error handling an update");
        }
    }
}
