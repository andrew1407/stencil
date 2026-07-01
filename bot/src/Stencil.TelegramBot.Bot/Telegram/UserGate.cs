using System.Collections.Concurrent;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// Per-user serialization gate: hands out a one-at-a-time async lock keyed by Telegram user id,
/// so a single user's operations never interleave while different users still run concurrently.
/// </summary>
/// <remarks>
/// The bot's session mutations are read-modify-write — load the
/// <see cref="Domain.Sessions.UserSession"/>, fold in an edit, save it back. Two updates from the
/// same user racing that sequence let a later save clobber an earlier one (a lost edit). Both
/// inbound entry points — the <see cref="UpdateRouter"/> for interactive updates and the
/// <see cref="SyncWatcher"/>'s background pull — acquire this gate around a user's work, making the
/// sequence serial per user without a lock in every service method. Cross-user throughput is
/// unaffected (each user has an independent semaphore).
///
/// One <see cref="SemaphoreSlim"/> is kept per user id for the process lifetime; each is a few
/// dozen bytes, so the map is left to grow rather than risk an acquire/evict race by pruning it.
/// This is a single-instance gate — horizontal scaling would need a distributed lock (e.g. Redis).
/// </remarks>
public sealed class UserGate
{
    private readonly ConcurrentDictionary<long, SemaphoreSlim> _gates = new();

    /// <summary>
    /// Acquire the given user's gate, waiting if another operation for that user holds it. Dispose
    /// the returned handle (via <c>using</c>) to release it.
    /// </summary>
    public async Task<IDisposable> AcquireAsync(long userId, CancellationToken ct = default)
    {
        SemaphoreSlim gate = _gates.GetOrAdd(userId, _ => new SemaphoreSlim(1, 1));
        await gate.WaitAsync(ct).ConfigureAwait(false);
        return new Releaser(gate);
    }

    /// <summary>Releases the held gate exactly once when disposed (double-dispose is a no-op).</summary>
    private sealed class Releaser : IDisposable
    {
        private readonly SemaphoreSlim _gate;
        private bool _released;

        public Releaser(SemaphoreSlim gate)
        {
            _gate = gate;
        }

        public void Dispose()
        {
            if (_released)
            {
                return;
            }
            _released = true;
            _gate.Release();
        }
    }
}
