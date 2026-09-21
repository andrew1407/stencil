using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Stencil.TelegramBot.Application.DependencyInjection;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Application.Servers;
using Stencil.TelegramBot.Bot;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Configuration;
using Stencil.TelegramBot.Domain.Llm;
using Stencil.TelegramBot.Infrastructure.Cli;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.DependencyInjection;
using Stencil.TelegramBot.Infrastructure.Llm;
using Stencil.TelegramBot.Infrastructure.Sessions;
using Telegram.Bot;
using Stencil.TelegramBot.Bot.Telegram.Sync;
using Stencil.TelegramBot.Domain.Llm.Wire;

namespace Stencil.TelegramBot.Tests.Configuration;

/// <summary>The DI wiring Program.cs relies on: a missing registration otherwise surfaces only on a real start, so every registered service is resolved here — no token dialed, no Redis, no hosted loop.</summary>
public sealed class CompositionRootTests : IDisposable
{
    // Well-formed but fake: TelegramBotClient parses "<id>:<hash>" and talks to nobody until used.
    private const string _fakeToken = "123456:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

    private readonly string _dataDir =
        Path.Combine(Path.GetTempPath(), "stencil-bot-di-" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        try { Directory.Delete(_dataDir, recursive: true); } catch { /* best effort */ }
    }

    private BotOptions makeOptions(string redisUrl = "") =>
        new() { DataDir = _dataDir, BotToken = _fakeToken, RedisUrl = redisUrl };

    private ServiceCollection wired(BotOptions? options = null)
    {
        ServiceCollection services = new();
        services.AddLogging();
        services.AddStencilBot(options ?? makeOptions(), new TelegramBotClient(_fakeToken));
        return services;
    }

    [Fact]
    public void Should_Resolve_Every_Registered_Service()
    {
        ServiceCollection services = wired();
        using ServiceProvider provider = services.BuildServiceProvider(validateScopes: true);

        List<string> unresolvable = new();
        foreach (ServiceDescriptor descriptor in services)
        {
            if (descriptor.ServiceType.IsGenericTypeDefinition)
            {
                continue; // open generics (ILogger<>) are resolved through their closed uses below
            }
            try
            {
                Assert.NotNull(provider.GetRequiredService(descriptor.ServiceType));
            }
            catch (Exception ex)
            {
                unresolvable.Add($"{descriptor.ServiceType.Name}: {ex.Message}");
            }
        }
        Assert.True(unresolvable.Count == 0, "unresolvable service(s):\n  " + string.Join("\n  ", unresolvable));
    }

    [Fact]
    public void Should_Resolve_The_Graph_The_Host_Pulls_Out_By_Hand()
    {
        using ServiceProvider provider = wired().BuildServiceProvider(validateScopes: true);

        // Exactly what Program.cs asks for after Build(), plus the two hosted loops it starts.
        Assert.NotNull(provider.GetRequiredService<UpdateRouter>());
        Assert.NotNull(provider.GetRequiredService<TelegramBotClient>());
        Assert.Equal(2, provider.GetServices<IHostedService>().Count());
        Assert.Contains(provider.GetServices<IHostedService>(), s => s is SyncWatcher);
        Assert.Contains(provider.GetServices<IHostedService>(), s => s is WorkspaceJanitor);
    }

    [Fact]
    public void Should_Reach_The_Policy_Under_Both_Its_Contract_And_Its_Record()
    {
        using ServiceProvider provider = wired().BuildServiceProvider(validateScopes: true);

        // Handlers name the Domain contract; the adapters name the record. One instance, both ways.
        Assert.Same(provider.GetRequiredService<BotOptions>(), provider.GetRequiredService<IBotPolicy>());
    }

    [Fact]
    public void Should_Register_The_Shared_Services_As_Singletons()
    {
        using ServiceProvider provider = wired().BuildServiceProvider(validateScopes: true);

        // PromptService owns the per-user chat history and SyncRegistry the live-sync set:
        // a second instance would silently split that state.
        Assert.Same(provider.GetRequiredService<PromptService>(), provider.GetRequiredService<PromptService>());
        Assert.Same(provider.GetRequiredService<SyncRegistry>(), provider.GetRequiredService<SyncRegistry>());
        Assert.Same(provider.GetRequiredService<ISessionStore>(), provider.GetRequiredService<ISessionStore>());
    }

    [Fact]
    public void Should_Register_The_Real_Infrastructure_Adapters()
    {
        using ServiceProvider provider = wired().BuildServiceProvider(validateScopes: true);

        Assert.IsType<ProcessStencilCli>(provider.GetRequiredService<IStencilCli>());
        Assert.IsType<HttpLlmClient>(provider.GetRequiredService<ILlmClient>());
        Assert.IsAssignableFrom<EditingService>(provider.GetRequiredService<IEditingService>());
        Assert.IsAssignableFrom<ServerService>(provider.GetRequiredService<IServerService>());
    }

    [Fact]
    public void Should_Use_The_In_Memory_Session_Store_Without_Redis()
    {
        using ServiceProvider provider = wired().BuildServiceProvider(validateScopes: true);

        Assert.IsType<InMemorySessionStore>(provider.GetRequiredService<ISessionStore>());
    }

    [Fact]
    public void Should_Use_The_Redis_Session_Store_With_Redis()
    {
        // Registration only — resolving it would dial the server, which this suite never does.
        ServiceCollection services = wired(makeOptions(redisUrl: "redis://localhost:6379"));

        ServiceDescriptor store = services.Last(d => d.ServiceType == typeof(ISessionStore));
        Assert.Equal(typeof(RedisSessionStore), store.ImplementationType);
    }

    [Fact]
    public void Should_Register_The_Application_Layer_On_Its_Own()
    {
        // AddStencilApplication is callable without the host: its dependencies come from
        // Infrastructure, so on its own it declares services but resolves none.
        ServiceCollection services = new();
        services.AddStencilApplication();

        Assert.Contains(services, d => d.ServiceType == typeof(IEditingService));
        Assert.Contains(services, d => d.ServiceType == typeof(IServerService));
        Assert.Contains(services, d => d.ServiceType == typeof(PromptService));
        Assert.Contains(services, d => d.ServiceType == typeof(LlmAttachmentLoader));
        Assert.All(services, d => Assert.Equal(ServiceLifetime.Singleton, d.Lifetime));
    }

    [Fact]
    public void Should_Register_The_Configured_Chat_Apis_And_Llm_Cap()
    {
        ServiceCollection services = new();
        services.AddLogging();
        services.AddStencilInfrastructure(makeOptions());
        using ServiceProvider provider = services.BuildServiceProvider(validateScopes: true);

        Assert.NotNull(provider.GetRequiredService<IReadOnlyList<LlmProfile>>());
        Assert.NotNull(provider.GetRequiredService<LlmGate>());
        Assert.Same(makeOptions().Llm.Provider, provider.GetRequiredService<LlmOptions>().Provider);
    }
}
