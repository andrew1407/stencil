using Microsoft.Extensions.DependencyInjection;
using Stencil.TelegramBot.Application.DependencyInjection;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.DependencyInjection;
using Stencil.TelegramBot.Infrastructure.Links;
using Telegram.Bot;

namespace Stencil.TelegramBot.Bot;

// Program.cs registers nothing else, so resolving this graph in a test is what proves the wiring.
public static class BotComposition
{
    public static IServiceCollection AddStencilBot(
        this IServiceCollection services, BotOptions options, TelegramBotClient client)
    {
        services.AddStencilInfrastructure(options);
        services.AddStencilApplication();
        services.AddSingleton(client);
        services.AddSingleton<ITelegramBotClient>(client);
        services.AddSingleton<SyncRegistry>();
        // Re-check the dialed address with /layout's predicate, closing the DNS-rebinding gap.
        services.AddSingleton(new LayoutFetcher(options, isBlockedAddress: RemoteImageUrl.IsBlockedAddress));
        services.AddSingleton<UserGate>();
        services.AddSingleton<PromptCancellations>();
        services.AddSingleton<CommandHandlers>();
        services.AddSingleton<CallbackAction>();
        services.AddSingleton<UpdateRouter>();
        // Hosted, so Ctrl+C/SIGTERM cancels and AWAITS both loops, never tearing one down
        // mid-sweep.
        services.AddHostedService<SyncWatcher>();
        services.AddHostedService<WorkspaceJanitor>();
        return services;
    }
}
