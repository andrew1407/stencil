using Microsoft.Extensions.DependencyInjection;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Servers;

namespace Stencil.TelegramBot.Application.DependencyInjection;

/// <summary>
/// Registers the Application layer's services. The host (Bot project) calls this once to wire
/// the editing/server services; their dependencies (CLI, server-client factory, session store,
/// workspace) come from the Infrastructure layer's own registration.
/// </summary>
public static class ServiceCollectionExtensions
{
    /// <summary>
    /// Add <see cref="IEditingService"/> and <see cref="IServerService"/>. Both are stateless
    /// (all per-user state lives in the session store), so singleton lifetimes are fine.
    /// </summary>
    public static IServiceCollection AddStencilApplication(this IServiceCollection services)
    {
        services.AddSingleton<IEditingService, EditingService>();
        services.AddSingleton<IServerService, ServerService>();
        return services;
    }
}
