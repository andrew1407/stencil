using System.Collections.Concurrent;

namespace Stencil.TelegramBot.Bot.Telegram.Access;

// The in-flight assistant turns, one per user, so ⏹ Stop can cancel its turn. A turn holds the user's
// UserGate for minutes, so UpdateRouter handles the stop token BEFORE the gate.
public sealed class PromptCancellations
{
    private readonly ConcurrentDictionary<long, CancellationTokenSource> _running = new();

    // The returned registration carries the token the turn must run under: the caller's ct linked
    // with the stop signal.
    public Registration Begin(long userId, CancellationToken ct)
    {
        CancellationTokenSource cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        // An overlapping older turn is cancelled so nothing is left running invisibly behind it.
        if (_running.TryRemove(userId, out CancellationTokenSource? stale))
        {
            cancelSource(stale);
        }
        _running[userId] = cts;
        return new Registration(this, userId, cts);
    }

    // False when nothing is running — the button outlived its turn, which the caller reports.
    public bool Cancel(long userId)
    {
        if (!_running.TryGetValue(userId, out CancellationTokenSource? cts))
        {
            return false;
        }
        return cancelSource(cts);
    }

    private static bool cancelSource(CancellationTokenSource cts)
    {
        try
        {
            cts.Cancel();
            return true;
        }
        catch (ObjectDisposedException)
        {
            return false; // the turn settled between the lookup and the cancel
        }
    }

    public sealed class Registration : IDisposable
    {
        private readonly PromptCancellations _owner;
        private readonly long _userId;
        private readonly CancellationTokenSource _cts;

        internal Registration(PromptCancellations owner, long userId, CancellationTokenSource cts)
        {
            _owner = owner;
            _userId = userId;
            _cts = cts;
        }

        public CancellationToken Token => _cts.Token;

        public void Dispose()
        {
            _owner._running.TryRemove(new KeyValuePair<long, CancellationTokenSource>(_userId, _cts));
            _cts.Dispose();
        }
    }
}
