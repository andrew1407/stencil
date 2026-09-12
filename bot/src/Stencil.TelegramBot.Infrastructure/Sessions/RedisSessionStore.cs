using System.Text.Json;
using StackExchange.Redis;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Infrastructure.Sessions;

// The same Redis the Go server uses, so multiple bot instances share per-user state; one JSON value
// per user.
public sealed class RedisSessionStore : ISessionStore
{
    private readonly IConnectionMultiplexer _redis;

    public RedisSessionStore(IConnectionMultiplexer redis)
    {
        _redis = redis;
    }

    public async Task<UserSession> GetAsync(long userId, CancellationToken ct = default)
    {
        IDatabase db = _redis.GetDatabase();
        RedisValue value = await db.StringGetAsync(KeyFor(userId)).ConfigureAwait(false);
        if (value.IsNullOrEmpty)
        {
            return new UserSession { UserId = userId };
        }
        UserSession? session = JsonSerializer.Deserialize<UserSession>((string)value!, StencilJson.Options);
        return session ?? new UserSession { UserId = userId };
    }

    public async Task SaveAsync(UserSession session, CancellationToken ct = default)
    {
        IDatabase db = _redis.GetDatabase();
        string json = StencilJson.Serialize(session);
        await db.StringSetAsync(KeyFor(session.UserId), json).ConfigureAwait(false);
    }

    public async Task ResetAsync(long userId, CancellationToken ct = default)
    {
        IDatabase db = _redis.GetDatabase();
        await db.KeyDeleteAsync(KeyFor(userId)).ConfigureAwait(false);
    }

    private static RedisKey KeyFor(long userId) => $"stencilbot:session:{userId}";
}
