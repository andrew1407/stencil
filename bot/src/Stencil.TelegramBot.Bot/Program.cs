using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.DependencyInjection;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.DependencyInjection;
using Stencil.TelegramBot.Infrastructure.Links;
using Telegram.Bot;

// Entry point for the Stencil Telegram bot, like the other front-ends' hosts: it wires the shared
// Application + Infrastructure services, then drives a thin Telegram presentation over them.

LoadDotEnvFiles();

BotOptions options = BotOptions.FromEnvironment();
if (string.IsNullOrWhiteSpace(options.BotToken))
{
    await Console.Error.WriteLineAsync(
        "Set TELEGRAM_BOT_TOKEN (copy bot/.env.example to bot/.env and paste your @BotFather token) to run the bot.");
    return 1;
}

HostApplicationBuilder host = Host.CreateApplicationBuilder(args);
host.Logging.ClearProviders();
host.Logging.AddConsole();
host.Logging.SetMinimumLevel(LogLevel.Information);
IServiceCollection services = host.Services;
services.AddStencilInfrastructure(options);
services.AddStencilApplication();
TelegramBotClient client = new(options.BotToken);
services.AddSingleton(client);
services.AddSingleton<ITelegramBotClient>(client);
services.AddSingleton<SyncRegistry>();
// Re-check the dialed address with /layout's own predicate, closing the DNS-rebinding gap.
services.AddSingleton(new LayoutFetcher(options, isBlockedAddress: RemoteImageUrl.IsBlockedAddress));
services.AddSingleton<UserGate>();
services.AddSingleton<PromptCancellations>();
services.AddSingleton<CommandHandlers>();
services.AddSingleton<CallbackAction>();
services.AddSingleton<UpdateRouter>();
// Hosted, so Ctrl+C/SIGTERM cancels and AWAITS both loops instead of tearing them down mid-sweep.
services.AddHostedService<SyncWatcher>();
services.AddHostedService<WorkspaceJanitor>();

using IHost app = host.Build();
ILogger<Program> logger = app.Services.GetRequiredService<ILogger<Program>>();
UpdateRouter router = app.Services.GetRequiredService<UpdateRouter>();
TelegramBotClient bot = app.Services.GetRequiredService<TelegramBotClient>();
// The host's lifetime owns shutdown: this token is what every in-flight update is cancelled by.
CancellationToken shutdown = app.Services.GetRequiredService<IHostApplicationLifetime>().ApplicationStopping;

// The update pump is SEQUENTIAL: Telegram.Bot awaits each handler before it delivers the next
// update. An assistant turn runs for minutes, so awaiting one here froze every other update —
// including the ⏹ Stop tap meant to end it, which then arrived long past the ~15 s window
// Telegram allows for answering a callback query ("query is too old"). So each update is
// detached onto its own task and the pump keeps moving; ordering within ONE user stays serial
// because UserGate — not this loop — is what enforces it.
bot.OnMessage += (message, _) => Detach(() => router.HandleMessageAsync(message, shutdown));
bot.OnUpdate += update => Detach(() => router.HandleUpdateAsync(update, shutdown));
bot.OnError += (exception, source) =>
{
    logger.LogError(exception, "Telegram polling error ({Source})", source);
    return Task.CompletedTask;
};

// Starts the hosted loops: the live-sync poller and the stale-scratch-file sweeper.
await app.StartAsync();

Telegram.Bot.Types.User me = await bot.GetMe(shutdown);
logger.LogInformation("@{Username} started", me.Username);

// Register the "/" command menu so it always matches the code (no manual BotFather upkeep).
try
{
    await bot.SetMyCommands(BotCommandList.All(), cancellationToken: shutdown);
}
catch (Exception ex)
{
    logger.LogWarning(ex, "Could not register the command menu");
}

await app.WaitForShutdownAsync();
logger.LogInformation("Shutting down");

return 0;

// Hand one update to the router on its own task, freeing the pump. The router already guards its
// work; this only keeps a cancelled/faulted task from surfacing unobserved at shutdown.
Task Detach(Func<Task> work)
{
    _ = Task.Run(async () =>
    {
        try
        {
            await work();
        }
        catch (OperationCanceledException)
        {
            // Shutdown — the router's guard lets these through on purpose.
        }
        catch (Exception ex)
        {
            logger.LogError(ex, "Unhandled error handling an update");
        }
    }, CancellationToken.None);
    return Task.CompletedTask;
}

// Best-effort discovery: app base dir, working dir, then the repo copy. Real env vars win.
static void LoadDotEnvFiles()
{
    DotEnv.Load(Path.Combine(AppContext.BaseDirectory, ".env"));
    DotEnv.Load(Path.Combine(Directory.GetCurrentDirectory(), ".env"));
    foreach (string candidate in RepoBotEnvCandidates())
    {
        DotEnv.Load(candidate);
    }
}

// Walk up from the working directory looking for a `bot/.env` (the repo layout), so a dev run
// from anywhere in the tree still picks up the token file.
static IEnumerable<string> RepoBotEnvCandidates()
{
    DirectoryInfo? dir = new(Directory.GetCurrentDirectory());
    for (int depth = 0; depth < 6 && dir is not null; depth++)
    {
        yield return Path.Combine(dir.FullName, "bot", ".env");
        dir = dir.Parent;
    }
}
