using System.Threading.Channels;
using Microsoft.Extensions.Logging;

namespace Stencil.TelegramBot.Bot;

// Telegram.Bot awaits each handler before the next update, and an assistant turn runs for minutes,
// so a bounded worker pool detaches each update; per-user ordering is UserGate's job, not this
// queue's.
public sealed class UpdatePump : IAsyncDisposable
{
    // Enough workers that a few minutes-long turns can't starve a Stop tap.
    private const int _workerCount = 32;
    private const int _capacity = 256;
    private static readonly TimeSpan _drainTimeout = TimeSpan.FromSeconds(10);

    private readonly Channel<Func<Task>> _queue = Channel.CreateBounded<Func<Task>>(
        new BoundedChannelOptions(_capacity) { FullMode = BoundedChannelFullMode.Wait });
    private readonly Task[] _workers;
    private readonly ILogger _logger;

    public UpdatePump(ILogger logger, int workers = _workerCount)
    {
        _logger = logger;
        _workers = [.. Enumerable.Range(0, workers).Select(_ => Task.Run(workAsync))];
    }

    public async Task EnqueueAsync(Func<Task> work) => await _queue.Writer.WriteAsync(work);

    public async ValueTask DisposeAsync()
    {
        _queue.Writer.TryComplete();
        // Bounded: shutdown already cancelled the handlers, so none can hold the process open.
        await Task.WhenAny(Task.WhenAll(_workers), Task.Delay(_drainTimeout));
    }

    private async Task workAsync()
    {
        await foreach (Func<Task> work in _queue.Reader.ReadAllAsync())
        {
            try
            {
                await work();
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
}
