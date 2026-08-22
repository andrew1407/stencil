using System.Collections.Concurrent;

namespace Stencil.TelegramBot.Bot.Telegram;

/// <summary>
/// The assistant turns currently in flight, one per user, so the ⏹ Stop button on the working
/// notice can cancel the turn it decorates.
/// </summary>
/// <remarks>
/// A turn can run for minutes (a vision plan over a big image), and while it runs it holds that
/// user's <see cref="UserGate"/> — so the Stop tap cannot be routed like any other button, or it
/// would queue behind the very turn it means to cancel. <see cref="UpdateRouter"/> therefore
/// handles the stop token BEFORE acquiring the gate, and this registry is the only state it
/// touches: cancelling is a flag flip, never a session read-modify-write, so it is safe to run
/// alongside the turn.
///
/// One entry exists only while a turn is in flight (<see cref="Registration.Dispose"/> removes
/// it), and a second turn for the same user cannot start while the first holds the gate, so the
/// map holds at most one live entry per user with no pruning needed.
/// </remarks>
public sealed class PromptCancellations
{
    private readonly ConcurrentDictionary<long, CancellationTokenSource> _running = new();

    /// <summary>
    /// Register an in-flight turn for <paramref name="userId"/>. The returned registration
    /// carries the token the turn must run under — the caller's <paramref name="ct"/> (shutdown)
    /// linked with this registry's stop signal — and unregisters on dispose.
    /// </summary>
    public Registration Begin(long userId, CancellationToken ct)
    {
        CancellationTokenSource cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        // A turn that somehow overlaps an older one takes the slot; the stale source is
        // cancelled so nothing is left running invisibly behind it.
        if (_running.TryRemove(userId, out CancellationTokenSource? stale))
        {
            Cancel(stale);
        }
        _running[userId] = cts;
        return new Registration(this, userId, cts);
    }

    /// <summary>
    /// Cancel the user's in-flight turn. False when there is nothing running — the button
    /// outlived its turn, which the caller reports rather than silently ignoring.
    /// </summary>
    public bool Cancel(long userId)
    {
        if (!_running.TryGetValue(userId, out CancellationTokenSource? cts))
        {
            return false;
        }
        return Cancel(cts);
    }

    private static bool Cancel(CancellationTokenSource cts)
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

    /// <summary>One registered turn; disposing it ends the registration and its token.</summary>
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

        /// <summary>The token the turn runs under: shutdown OR a Stop tap cancels it.</summary>
        public CancellationToken Token => _cts.Token;

        public void Dispose()
        {
            _owner._running.TryRemove(new KeyValuePair<long, CancellationTokenSource>(_userId, _cts));
            _cts.Dispose();
        }
    }
}
