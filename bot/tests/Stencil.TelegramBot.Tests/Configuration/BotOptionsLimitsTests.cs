using Stencil.TelegramBot.Application.Editing;
using Stencil.TelegramBot.Infrastructure.Configuration;

namespace Stencil.TelegramBot.Tests.Configuration;

/// <summary>The <see cref="BotOptions"/> knobs behind the update pump, the fetch guard, the progress notice, the sync poller, the album buffer and the server client: their defaults are the values that used to be literals, each reads its env key, a malformed value keeps the default, and the project page never asks the server for more than it serves.</summary>
public sealed class BotOptionsLimitsTests
{
    [Fact]
    public void Should_Default_Every_Limit_To_Its_Former_Literal()
    {
        BotOptions options = new();

        Assert.Equal(32, options.UpdateWorkers);
        Assert.Equal(256, options.UpdateQueueCapacity);
        Assert.Equal(64, options.MaxPendingPerUser);
        Assert.Equal(TimeSpan.FromSeconds(10), options.ShutdownDrainTimeout);
        Assert.Equal(TimeSpan.FromSeconds(3), options.ProgressTick);
        Assert.Equal(TimeSpan.FromSeconds(6), options.SyncPollInterval);
        Assert.Equal(TimeSpan.FromSeconds(1), options.AlbumSettle);
        Assert.True(options.AllowPrivateServers);
        Assert.Equal(64L * 1024 * 1024, options.MaxServerResponseBytes);
        Assert.Equal(200, options.ProjectListLimit);
        Assert.Equal(TimeSpan.FromMinutes(5), options.RefusalReplyWindow);
    }

    [Fact]
    public void Should_Agree_With_The_Guards_Own_Fallback_On_The_Resolve_Timeout()
    {
        Assert.Equal(TimeSpan.FromSeconds(5), new BotOptions().ResolveTimeout);
        Assert.Equal(RemoteImageUrl.DefaultResolveTimeout, new BotOptions().ResolveTimeout);
    }

    [Fact]
    public void Should_Parse_Every_Limit_From_Environment()
    {
        withEnv("STENCIL_BOT_UPDATE_WORKERS", "4", o => Assert.Equal(4, o.UpdateWorkers));
        withEnv("STENCIL_BOT_UPDATE_QUEUE", "9", o => Assert.Equal(9, o.UpdateQueueCapacity));
        withEnv("STENCIL_BOT_MAX_PENDING_PER_USER", "5", o => Assert.Equal(5, o.MaxPendingPerUser));
        withEnv("STENCIL_BOT_DRAIN_TIMEOUT_SECONDS", "2", o => Assert.Equal(TimeSpan.FromSeconds(2), o.ShutdownDrainTimeout));
        withEnv("STENCIL_BOT_RESOLVE_TIMEOUT_SECONDS", "8", o => Assert.Equal(TimeSpan.FromSeconds(8), o.ResolveTimeout));
        withEnv("STENCIL_BOT_PROGRESS_TICK_SECONDS", "6", o => Assert.Equal(TimeSpan.FromSeconds(6), o.ProgressTick));
        withEnv("STENCIL_BOT_SYNC_POLL_SECONDS", "15", o => Assert.Equal(TimeSpan.FromSeconds(15), o.SyncPollInterval));
        withEnv("STENCIL_BOT_ALBUM_SETTLE_MS", "400", o => Assert.Equal(TimeSpan.FromMilliseconds(400), o.AlbumSettle));
        withEnv("STENCIL_BOT_WORKSPACE_SWEEP_FLOOR_MINUTES", "2", o => Assert.Equal(TimeSpan.FromMinutes(2), o.WorkspaceSweepFloor));
        withEnv("STENCIL_BOT_MAX_SERVER_RESPONSE_MB", "3", o => Assert.Equal(3L * 1024 * 1024, o.MaxServerResponseBytes));
        withEnv("STENCIL_BOT_PROJECT_LIST_LIMIT", "25", o => Assert.Equal(25, o.ProjectListLimit));
        withEnv("STENCIL_BOT_REFUSAL_WINDOW_SECONDS", "30", o => Assert.Equal(TimeSpan.FromSeconds(30), o.RefusalReplyWindow));
        withEnv("STENCIL_BOT_UPDATE_WORKERS", "0", o => Assert.Equal(32, o.UpdateWorkers));
        withEnv("STENCIL_BOT_REFUSAL_WINDOW_SECONDS", "0", o => Assert.Equal(TimeSpan.FromMinutes(5), o.RefusalReplyWindow));
        withEnv("STENCIL_BOT_PROGRESS_TICK_SECONDS", "soon", o => Assert.Equal(TimeSpan.FromSeconds(3), o.ProgressTick));
        withEnv("STENCIL_BOT_SYNC_POLL_SECONDS", "-1", o => Assert.Equal(TimeSpan.FromSeconds(6), o.SyncPollInterval));
        withEnv("STENCIL_BOT_ALBUM_SETTLE_MS", "0", o => Assert.Equal(TimeSpan.FromSeconds(1), o.AlbumSettle));
        withEnv("STENCIL_BOT_WORKSPACE_SWEEP_FLOOR_MINUTES", "0", o => Assert.Equal(TimeSpan.FromMinutes(5), o.WorkspaceSweepFloor));
    }

    [Fact]
    public void Should_Clamp_The_Project_Page_To_The_Servers_Maximum()
    {
        withEnv("STENCIL_BOT_PROJECT_LIST_LIMIT", "5000", o => Assert.Equal(BotOptions.MAX_PROJECT_LIST_LIMIT, o.ProjectListLimit));
    }

    [Theory]
    [InlineData("0", false)]
    [InlineData("false", false)]
    [InlineData(" Off ", false)]
    [InlineData("no", false)]
    [InlineData("1", true)]
    [InlineData("on", true)]
    [InlineData("", true)]          // unset keeps today's behaviour
    [InlineData("nope", true)]      // a typo keeps it too
    public void Should_Read_The_Private_Server_Switch_From_Environment(string value, bool expected)
    {
        withEnv("STENCIL_BOT_ALLOW_PRIVATE_SERVERS", value, o => Assert.Equal(expected, o.AllowPrivateServers));
    }

    private static void withEnv(string name, string value, Action<BotOptions> check)
    {
        string? original = Environment.GetEnvironmentVariable(name);
        try
        {
            Environment.SetEnvironmentVariable(name, value);
            check(BotOptions.FromEnvironment());
        }
        finally
        {
            Environment.SetEnvironmentVariable(name, original);
        }
    }
}
