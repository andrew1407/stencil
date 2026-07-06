using System.Text.Json;
using StackExchange.Redis;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Serialization;
using Stencil.TelegramBot.Domain.Sessions;

namespace Stencil.TelegramBot.Infrastructure.Sessions;

/// <summary>
/// A Redis-backed <see cref="ISessionStore"/> — the same store the Go server uses for
/// cross-instance fan-out — so multiple bot instances share per-user state. Sessions are stored
/// as one JSON value per user under <c>stencilbot:session:{userId}</c>.
/// </summary>
public sealed class RedisSessionStore : ISessionStore
{
    private readonly IConnectionMultiplexer _redis;

    public RedisSessionStore(IConnectionMultiplexer redis)
    {
        _redis = redis;
    }

    /// <summary>Load and deserialise the user's session, or a fresh empty one when the key is absent.</summary>
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

    /// <summary>Serialise and persist the session (overwrites the prior value for this user).</summary>
    public async Task SaveAsync(UserSession session, CancellationToken ct = default)
    {
        IDatabase db = _redis.GetDatabase();
        string json = StencilJson.Serialize(session);
        await db.StringSetAsync(KeyFor(session.UserId), json).ConfigureAwait(false);
    }

    /// <summary>Drop the user's stored session entirely.</summary>
    public async Task ResetAsync(long userId, CancellationToken ct = default)
    {
        IDatabase db = _redis.GetDatabase();
        await db.KeyDeleteAsync(KeyFor(userId)).ConfigureAwait(false);
    }

    /// <summary>The Redis key for one user's session JSON.</summary>
    private static RedisKey KeyFor(long userId) => $"stencilbot:session:{userId}";
}
