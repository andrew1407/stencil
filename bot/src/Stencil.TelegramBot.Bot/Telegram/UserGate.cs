namespace Stencil.TelegramBot.Bot.Telegram;

// A one-at-a-time async lock per Telegram user: session mutations are read-modify-write, so two
// updates from one user racing lose an edit. UpdateRouter and SyncWatcher both take it. Each
// semaphore is reference-counted and dropped with its last holder. Single-instance: horizontal
// scaling would need a distributed lock.
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
            drop(userId, gate);
            throw;
        }
        return new Releaser(this, userId, gate);
    }

    // Forgotten only when no holder is left, so a caller can never be handed a replacement while
    // still holding it.
    private void drop(long userId, Gate gate)
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
            _owner.drop(_userId, _gate);
        }
    }
}
