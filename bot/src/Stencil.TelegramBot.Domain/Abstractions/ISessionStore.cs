using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Domain.Abstractions;

// Redis when REDIS_URL is configured (the store the Go server uses for cross-instance fan-out),
// an in-memory map otherwise — so dev and the test suite need no external services.
public interface ISessionStore
{
    // A fresh empty session when none is stored yet.
    Task<UserSession> GetAsync(long userId, CancellationToken ct = default);

    Task SaveAsync(UserSession session, CancellationToken ct = default);

    Task ResetAsync(long userId, CancellationToken ct = default);
}
