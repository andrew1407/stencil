namespace Stencil.TelegramBot.Bot.Telegram;

// A one-at-a-time async lock keyed by Telegram user id. The bot's session mutations are
// read-modify-write — load the UserSession, fold in an edit, save it back — so two updates from
// the same user racing that sequence lose an edit. Both inbound entry points (UpdateRouter and
// SyncWatcher's background pull) take this, which makes the sequence serial per user without a
// lock in every service method; different users keep their own semaphore and stay concurrent.
// Each semaphore is reference-counted and dropped once its last holder releases, so the map
// holds the users acting now, not everyone who ever wrote. Single-instance: horizontal scaling
// would need a distributed lock.
public sealed class UserGate
{
    private readonly Dictionary<long, Gate> _gates = [];

    // How many users hold or await a gate; the map never outgrows this.
    public int TrackedUsers
    {
        get
        {
            lock (_gates)
            {
                return _gates.Count;
            }
        }
    }

    // Dispose the returned handle (via `using`) to release it.
    public async Task<IDisposable> AcquireAsync(long userId, CancellationToken ct = default)
    {
        Gate gate;
        lock (_gates)
        {
            if (!_gates.TryGetValue(userId, out gate!))
            {
                _gates[userId] = gate = new Gate();
            }
            gate.Holders++;
        }
        try
        {
            await gate.Semaphore.WaitAsync(ct).ConfigureAwait(false);
        }
        catch
        {
            Drop(userId, gate);
            throw;
        }
        return new Releaser(this, userId, gate);
    }

    // The semaphore is forgotten only when no holder is left, so a caller that took it before
    // the eviction can never be handed a replacement while it still holds this one.
    private void Drop(long userId, Gate gate)
    {
        lock (_gates)
        {
            if (--gate.Holders == 0 && _gates.TryGetValue(userId, out Gate? current) && current == gate)
            {
                _gates.Remove(userId);
                gate.Semaphore.Dispose();
            }
        }
    }

    private sealed class Gate
    {
        public readonly SemaphoreSlim Semaphore = new(1, 1);
        public int Holders;
    }

    // Releases exactly once; double-dispose is a no-op.
    private sealed class Releaser : IDisposable
    {
        private readonly UserGate _owner;
        private readonly long _userId;
        private readonly Gate _gate;
        private bool _released;

        public Releaser(UserGate owner, long userId, Gate gate)
        {
            _owner = owner;
            _userId = userId;
            _gate = gate;
        }

        public void Dispose()
        {
            if (_released)
            {
                return;
            }
            _released = true;
            _gate.Semaphore.Release();
            _owner.Drop(_userId, _gate);
        }
    }
}
