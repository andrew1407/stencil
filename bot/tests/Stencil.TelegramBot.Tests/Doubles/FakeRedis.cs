using System.Reflection;
using StackExchange.Redis;

namespace Stencil.TelegramBot.Tests.Doubles;

/// <summary>A one-keyspace Redis behind the session store: <c>GetDatabase</c>, then string get, set
/// and key delete, over <see cref="DispatchProxy"/> so no client package is needed; any other call throws.</summary>
public class FakeRedis : DispatchProxy
{
    private FakeRedis? _db;

    public Dictionary<string, string> Values { get; private set; } = new();

    public static (IConnectionMultiplexer Redis, Dictionary<string, string> Values) Create()
    {
        IDatabase db = Create<IDatabase, FakeRedis>();
        IConnectionMultiplexer redis = Create<IConnectionMultiplexer, FakeRedis>();
        FakeRedis dbState = (FakeRedis)(object)db;
        ((FakeRedis)(object)redis)._db = dbState;
        return (redis, dbState.Values);
    }

    protected override object? Invoke(MethodInfo? method, object?[]? args)
    {
        object? first = args is { Length: > 0 } ? args[0] : null;
        string key = first is RedisKey k ? k.ToString() : "";
        if (method?.Name == "GetDatabase") return _db;
        if (first is RedisKey && method?.Name == "StringGetAsync")
            return Task.FromResult(Values.TryGetValue(key, out string? v) ? (RedisValue)v : RedisValue.Null);
        if (first is RedisKey && method?.Name == "StringSetAsync" && args![1] is RedisValue value)
        {
            Values[key] = value.ToString();
            return Task.FromResult(true);
        }
        if (first is RedisKey && method?.Name == "KeyDeleteAsync") return Task.FromResult(Values.Remove(key));
        throw new NotSupportedException($"FakeRedis does not serve {method?.Name}");
    }
}
