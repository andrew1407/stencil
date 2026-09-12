using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Domain.Abstractions;

public interface ISessionStore
{
    // A fresh empty session when none is stored yet.
    Task<UserSession> GetAsync(long userId, CancellationToken ct = default);

    Task SaveAsync(UserSession session, CancellationToken ct = default);

    Task ResetAsync(long userId, CancellationToken ct = default);
}
