using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Domain.Abstractions;

/// <summary>
/// Per-user session persistence. Backed by Redis when a <c>REDIS_URL</c> is configured
/// (the same store the Go server uses for cross-instance fan-out), or an in-memory map
/// otherwise — so basic dev and the test suite need no external services.
/// </summary>
public interface ISessionStore
{
    /// <summary>Load the user's session, or a fresh empty one if none is stored yet.</summary>
    Task<UserSession> GetAsync(long userId, CancellationToken ct = default);

    /// <summary>Persist the session (overwrites the prior value for this user).</summary>
    Task SaveAsync(UserSession session, CancellationToken ct = default);

    /// <summary>Drop the user's stored session entirely.</summary>
    Task ResetAsync(long userId, CancellationToken ct = default);
}
