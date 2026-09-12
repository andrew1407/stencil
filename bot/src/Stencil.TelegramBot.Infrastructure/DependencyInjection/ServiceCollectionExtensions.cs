using Microsoft.Extensions.DependencyInjection;
using StackExchange.Redis;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Configuration;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Cli;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.Llm;
using Stencil.TelegramBot.Infrastructure.Media;
using Stencil.TelegramBot.Infrastructure.Server;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Stencil.TelegramBot.Infrastructure.Workspace;

namespace Stencil.TelegramBot.Infrastructure.DependencyInjection;

public static class ServiceCollectionExtensions
{
    // Redis-backed session store when BotOptions.RedisUrl is set, in-memory otherwise.
    public static IServiceCollection AddStencilInfrastructure(this IServiceCollection services, BotOptions options)
    {
        services.AddSingleton(options);
        // Handlers and the janitor name the Domain contract, not the record: register both.
        services.AddSingleton<IBotPolicy>(options);
        services.AddSingleton(options.Llm);
        // Empty when the operator configured none, which leaves every turn on options.Llm.
        services.AddSingleton<IReadOnlyList<LlmProfile>>(options.LlmProfiles);
        // The process-wide LLM in-flight cap (full ⇒ an immediate busy reply, never a queue).
        services.AddSingleton(new LlmGate(options.MaxConcurrentLlm));
        services.AddSingleton<IUserWorkspace, UserWorkspace>();
        services.AddSingleton<IStencilCli, ProcessStencilCli>();
        services.AddSingleton<StencilServerClientFactory>();
        services.AddSingleton<IStencilServerClientFactory>(
            static sp => sp.GetRequiredService<StencilServerClientFactory>());
        // One HttpClient over the factory's pooled handler with the contract's slow-call timeout.
        // The endpoint comes only from explicit configuration, never from fetched content.
        services.AddSingleton<ILlmClient>(sp => new HttpLlmClient(
            sp.GetRequiredService<StencilServerClientFactory>().CreateHttpClient(HttpLlmClient.DefaultTimeout),
            options.Llm));
        services.AddSingleton<IImageDownscaler, FfmpegImageDownscaler>();

        if (string.IsNullOrWhiteSpace(options.RedisUrl))
        {
            services.AddSingleton<ISessionStore, InMemorySessionStore>();
        }
        else
        {
            // Through RedisConnectionString so the documented redis:// URL works, not just
            // StackExchange's syntax.
            services.AddSingleton<IConnectionMultiplexer>(
                _ => ConnectionMultiplexer.Connect(RedisConnectionString.Parse(options.RedisUrl)));
            services.AddSingleton<ISessionStore, RedisSessionStore>();
        }

        return services;
    }
}
