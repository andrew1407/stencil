using System.Collections.Concurrent;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Infrastructure.Sessions;

// The default store without REDIS_URL, so dev and the test suite need no external services.
public sealed class InMemorySessionStore : ISessionStore
{
    private readonly ConcurrentDictionary<long, UserSession> _sessions = new();

    public Task<UserSession> GetAsync(long userId, CancellationToken ct = default)
    {
        UserSession session = _sessions.TryGetValue(userId, out UserSession? stored)
            ? stored
            : new UserSession { UserId = userId };
        return Task.FromResult(session);
    }

    public Task SaveAsync(UserSession session, CancellationToken ct = default)
    {
        _sessions[session.UserId] = session;
        return Task.CompletedTask;
    }

    public Task ResetAsync(long userId, CancellationToken ct = default)
    {
        _sessions.TryRemove(userId, out _);
        return Task.CompletedTask;
    }
}
