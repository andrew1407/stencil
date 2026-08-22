using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.DependencyInjection;
using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Stencil.TelegramBot.Infrastructure.DependencyInjection;
using Stencil.TelegramBot.Infrastructure.Links;
using Telegram.Bot;

// Entry point for the Stencil Telegram bot. Mirrors the role of the other front-ends' hosts
// (the CLI's main, the desktop's main, pystencil's entry): it wires the shared Application +
// Infrastructure services, then drives a thin Telegram presentation layer over them.

LoadDotEnvFiles();

BotOptions options = BotOptions.FromEnvironment();
if (string.IsNullOrWhiteSpace(options.BotToken))
{
    await Console.Error.WriteLineAsync(
        "Set TELEGRAM_BOT_TOKEN (copy bot/.env.example to bot/.env and paste your @BotFather token) to run the bot.");
    return 1;
}

ServiceCollection services = new();
services.AddLogging(builder =>
{
    builder.AddConsole();
    builder.SetMinimumLevel(LogLevel.Information);
});
services.AddStencilInfrastructure(options);
services.AddStencilApplication();
TelegramBotClient client = new(options.BotToken);
services.AddSingleton(client);
services.AddSingleton<ITelegramBotClient>(client);
services.AddSingleton<SyncRegistry>();
// Re-check the dialed address with the same predicate /layout's pre-check uses, closing
// the DNS-rebinding gap between vet and fetch.
services.AddSingleton(new LayoutFetcher(options, isBlockedAddress: RemoteImageUrl.IsBlockedAddress));
services.AddSingleton<UserGate>();
services.AddSingleton<PromptCancellations>();
services.AddSingleton<CommandHandlers>();
services.AddSingleton<CallbackAction>();
services.AddSingleton<UpdateRouter>();
services.AddSingleton<SyncWatcher>();
services.AddSingleton<WorkspaceJanitor>();

await using ServiceProvider provider = services.BuildServiceProvider();
ILogger<Program> logger = provider.GetRequiredService<ILogger<Program>>();
UpdateRouter router = provider.GetRequiredService<UpdateRouter>();
TelegramBotClient bot = provider.GetRequiredService<TelegramBotClient>();

using CancellationTokenSource cts = new();
Console.CancelKeyPress += (_, eventArgs) =>
{
    eventArgs.Cancel = true;
    cts.Cancel();
};

// The update pump is SEQUENTIAL: Telegram.Bot awaits each handler before it delivers the next
// update. An assistant turn runs for minutes, so awaiting one here froze every other update —
// including the ⏹ Stop tap meant to end it, which then arrived long past the ~15 s window
// Telegram allows for answering a callback query ("query is too old"). So each update is
// detached onto its own task and the pump keeps moving; ordering within ONE user stays serial
// because UserGate — not this loop — is what enforces it.
bot.OnMessage += (message, _) => Detach(() => router.HandleMessageAsync(message, cts.Token));
bot.OnUpdate += update => Detach(() => router.HandleUpdateAsync(update, cts.Token));
bot.OnError += (exception, source) =>
{
    logger.LogError(exception, "Telegram polling error ({Source})", source);
    return Task.CompletedTask;
};

// Background live-sync poller (auto-pull peers' changes for /sync-enabled users).
SyncWatcher watcher = provider.GetRequiredService<SyncWatcher>();
Task syncLoop = watcher.RunAsync(cts.Token);

// Background sweeper that clears orphaned per-user scratch files once they age past the TTL.
WorkspaceJanitor janitor = provider.GetRequiredService<WorkspaceJanitor>();
Task janitorLoop = janitor.RunAsync(cts.Token);

Telegram.Bot.Types.User me = await bot.GetMe(cts.Token);
logger.LogInformation("@{Username} started", me.Username);

// Register the "/" command menu so it always matches the code (no manual BotFather upkeep).
try
{
    await bot.SetMyCommands(BotCommandList.All(), cancellationToken: cts.Token);
}
catch (Exception ex)
{
    logger.LogWarning(ex, "Could not register the command menu");
}

try
{
    await Task.Delay(Timeout.Infinite, cts.Token);
}
catch (OperationCanceledException)
{
    logger.LogInformation("Shutting down");
}

return 0;

// Hand one update to the router on its own task, so the caller (Telegram's update pump) is
// free immediately. The router already wraps its work in an error guard; this only has to keep
// a cancelled or faulted task from surfacing as an unobserved exception at shutdown.
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

// Best-effort .env discovery: the app base dir, the current working dir, and (when running
// from inside the repo) the repo's bot/.env. Real environment variables always win.
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
