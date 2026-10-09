using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Stencil.TelegramBot.Application.Llm.Plan;
using Stencil.TelegramBot.Bot;
using Stencil.TelegramBot.Bot.Telegram;
using Stencil.TelegramBot.Domain.Abstractions;
using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Configuration;
using Telegram.Bot;
using Stencil.TelegramBot.Bot.Telegram.Commands;

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
TelegramBotClient client = new(options.BotToken);
host.Services.AddStencilBot(options, client);

using IHost app = host.Build();
ILogger<Program> logger = app.Services.GetRequiredService<ILogger<Program>>();
UpdateRouter router = app.Services.GetRequiredService<UpdateRouter>();
TelegramBotClient bot = app.Services.GetRequiredService<TelegramBotClient>();
// The host's lifetime owns shutdown: this token is what every in-flight update is cancelled by.
CancellationToken shutdown = app.Services.GetRequiredService<IHostApplicationLifetime>().ApplicationStopping;

// Handlers run off the polling loop, one lane per user on the pump — see UpdatePump for why.
await using UpdatePump pump = new(logger, options);
UpdateIntake intake = new(pump, router, shutdown);
bot.OnMessage += intake.OnMessageAsync;
bot.OnUpdate += intake.OnUpdateAsync;
bot.OnError += (exception, source) =>
{
    logger.LogError(exception, "Telegram polling error ({Source})", source);
    return Task.CompletedTask;
};

await app.StartAsync(); // starts the hosted loops: the sync poller and the scratch sweeper

// A CLI built from another opRegistry.json would judge plans against ops the prompt never described.
try
{
    if (await OpPlanParser.RegistrySkewAsync(app.Services.GetRequiredService<IStencilCli>(), shutdown) is string skew)
    {
        logger.LogWarning("Op registry skew: {Skew}", skew);
    }
}
catch (StencilCliException ex)
{
    logger.LogWarning("Could not check the CLI's op registry: {Reason}", ex.OperatorDetail ?? ex.Message);
}

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

static IEnumerable<string> RepoBotEnvCandidates()
{
    DirectoryInfo? dir = new(Directory.GetCurrentDirectory());
    for (int depth = 0; depth < 6 && dir is not null; depth++)
    {
        yield return Path.Combine(dir.FullName, "bot", ".env");
        dir = dir.Parent;
    }
}
