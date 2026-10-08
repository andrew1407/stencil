using System.Text.Json;
using StackExchange.Redis;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Infrastructure.Sessions;

// One JSON value per user, in the Redis the Go server may share. It outlives a restart; it does not
// make the bot multi-instance, since UserGate and the update lanes are per-process.
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
        RedisValue value = await db.StringGetAsync(keyFor(userId)).ConfigureAwait(false);
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
        await db.StringSetAsync(keyFor(session.UserId), json).ConfigureAwait(false);
    }

    public async Task ResetAsync(long userId, CancellationToken ct = default)
    {
        IDatabase db = _redis.GetDatabase();
        await db.KeyDeleteAsync(keyFor(userId)).ConfigureAwait(false);
    }

    private static RedisKey keyFor(long userId) => $"stencilbot:session:{userId}";
}
