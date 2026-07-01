using Microsoft.Extensions.DependencyInjection;
using StackExchange.Redis;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Infrastructure.Cli;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Server;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;

namespace Stencil.TelegramBot.Infrastructure.DependencyInjection;

/// <summary>
/// Wires the Infrastructure adapters into a DI container: the CLI pixel engine, the
/// collaboration-server client factory, the per-user workspace, and the session store
/// (Redis-backed when a <c>REDIS_URL</c> is configured, in-memory otherwise).
/// </summary>
public static class ServiceCollectionExtensions
{
    /// <summary>
    /// Register every Infrastructure service against the given <paramref name="options"/>.
    /// When <see cref="BotOptions.RedisUrl"/> is set, the session store is backed by a shared
    /// <see cref="IConnectionMultiplexer"/>; otherwise it is the in-memory store.
    /// </summary>
    public static IServiceCollection AddStencilInfrastructure(this IServiceCollection services, BotOptions options)
    {
        services.AddSingleton(options);
        services.AddSingleton<IUserWorkspace, UserWorkspace>();
        services.AddSingleton<IStencilCli, ProcessStencilCli>();
        services.AddSingleton<IStencilServerClientFactory, StencilServerClientFactory>();

        if (string.IsNullOrWhiteSpace(options.RedisUrl))
        {
            services.AddSingleton<ISessionStore, InMemorySessionStore>();
        }
        else
        {
            services.AddSingleton<IConnectionMultiplexer>(_ => ConnectionMultiplexer.Connect(options.RedisUrl));
            services.AddSingleton<ISessionStore, RedisSessionStore>();
        }

        return services;
    }
}
