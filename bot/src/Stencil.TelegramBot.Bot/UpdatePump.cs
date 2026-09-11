using System.Threading.Channels;
using Microsoft.Extensions.Logging;

namespace Stencil.TelegramBot.Bot;

/// <summary>Bounded hand-off from Telegram's polling loop to a fixed set of update workers.</summary>
/// <remarks>
/// Telegram.Bot awaits each handler before delivering the next update and an assistant turn runs
/// for minutes, so awaiting one on the loop froze every other update — including the ⏹ Stop tap
/// meant to end it, arriving past the ~15 s callback-answer window. Detaching each update instead
/// left the task count unbounded; this queue keeps the detach but caps it. Per-user ordering is
/// <c>UserGate</c>'s job, not this queue's.
/// </remarks>
public sealed class UpdatePump : IAsyncDisposable
{
    // Enough workers that a few minutes-long turns can't starve a Stop tap.
    private const int Workers = 32;
    private const int Capacity = 256;
    private static readonly TimeSpan DrainTimeout = TimeSpan.FromSeconds(10);

    private readonly Channel<Func<Task>> _queue = Channel.CreateBounded<Func<Task>>(
        new BoundedChannelOptions(Capacity) { FullMode = BoundedChannelFullMode.Wait });
    private readonly Task[] _workers;
    private readonly ILogger _logger;

    public UpdatePump(ILogger logger, int workers = Workers)
    {
        _logger = logger;
        _workers = [.. Enumerable.Range(0, workers).Select(_ => Task.Run(WorkAsync))];
    }

    public async Task EnqueueAsync(Func<Task> work) => await _queue.Writer.WriteAsync(work);

    /// <summary>Stop accepting work and await whatever is still queued or in flight.</summary>
    public async ValueTask DisposeAsync()
    {
        _queue.Writer.TryComplete();
        // Bounded: shutdown already cancelled the handlers, so none can hold the process open.
        await Task.WhenAny(Task.WhenAll(_workers), Task.Delay(DrainTimeout));
    }

    private async Task WorkAsync()
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
