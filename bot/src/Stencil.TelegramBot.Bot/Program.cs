using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.DependencyInjection;
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
services.AddSingleton(new LayoutFetcher(options));
services.AddSingleton<UserGate>();
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

bot.OnMessage += async (message, _) => await router.HandleMessageAsync(message, cts.Token);
bot.OnUpdate += async update => await router.HandleUpdateAsync(update, cts.Token);
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
