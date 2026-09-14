using Microsoft.Extensions.DependencyInjection;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Application.Llm;
using Stencil.TelegramBot.Application.Servers;

namespace Stencil.TelegramBot.Application.DependencyInjection;

public static class ServiceCollectionExtensions
{
    // Stateless (per-user state lives in the session store), so singleton lifetimes are fine.
    public static IServiceCollection AddStencilApplication(this IServiceCollection services)
    {
        services.AddSingleton<IEditingService, EditingService>();
        services.AddSingleton<IServerService, ServerService>();
        // Singleton on purpose: it owns the per-user in-memory LLM chat history.
        services.AddSingleton<PromptService>();
        services.AddSingleton<LlmAttachmentLoader>();
        services.AddSingleton<IScriptService, ScriptService>();
        return services;
    }
}
