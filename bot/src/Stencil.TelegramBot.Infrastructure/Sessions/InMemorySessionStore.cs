using System.Collections.Concurrent;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Infrastructure.Sessions;

/// <summary>
/// A process-local <see cref="ISessionStore"/> over a <see cref="ConcurrentDictionary{TKey,TValue}"/>.
/// The default store when no <c>REDIS_URL</c> is configured, so basic dev and the test suite
/// need no external services.
/// </summary>
public sealed class InMemorySessionStore : ISessionStore
{
    private readonly ConcurrentDictionary<long, UserSession> _sessions = new();

    /// <summary>Return the stored session, or a fresh empty one keyed by <paramref name="userId"/>.</summary>
    public Task<UserSession> GetAsync(long userId, CancellationToken ct = default)
    {
        UserSession session = _sessions.TryGetValue(userId, out UserSession? stored)
            ? stored
            : new UserSession { UserId = userId };
        return Task.FromResult(session);
    }

    /// <summary>Persist the session (overwrites the prior value for this user).</summary>
    public Task SaveAsync(UserSession session, CancellationToken ct = default)
    {
        _sessions[session.UserId] = session;
        return Task.CompletedTask;
    }

    /// <summary>Drop the user's stored session entirely.</summary>
    public Task ResetAsync(long userId, CancellationToken ct = default)
    {
        _sessions.TryRemove(userId, out _);
        return Task.CompletedTask;
    }
}
