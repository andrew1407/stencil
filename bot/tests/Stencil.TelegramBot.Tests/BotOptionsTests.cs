using Stencil.TelegramBot.Infrastructure.Configuration;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// <see cref="BotOptions"/> configuration reading — focused on the CLI concurrency cap, which
/// defaults to the CPU count and clamps non-positive/garbage values back to that default.
/// </summary>
public sealed class BotOptionsTests
{
    private const string MaxConcurrentCliVar = "STENCIL_BOT_MAX_CONCURRENT_CLI";

    [Fact]
    public void MaxConcurrentCliDefaultsToProcessorCount()
    {
        BotOptions options = new();
        Assert.Equal(Math.Max(1, Environment.ProcessorCount), options.MaxConcurrentCli);
    }

    [Fact]
    public void LoadKnobsHaveSensibleDefaults()
    {
        BotOptions options = new();
        Assert.Equal(TimeSpan.FromSeconds(30), options.ServerHttpTimeout);
        Assert.Equal(50L * 1024 * 1024, options.MaxDownloadBytes);
        Assert.Equal(TimeSpan.FromMinutes(60), options.WorkspaceTtl);
    }

    [Fact]
    public void FromEnvironmentParsesLoadKnobs()
    {
        WithEnv("STENCIL_BOT_HTTP_TIMEOUT_SECONDS", "12", () =>
            Assert.Equal(TimeSpan.FromSeconds(12), BotOptions.FromEnvironment().ServerHttpTimeout));
        WithEnv("STENCIL_BOT_MAX_DOWNLOAD_MB", "7", () =>
            Assert.Equal(7L * 1024 * 1024, BotOptions.FromEnvironment().MaxDownloadBytes));
        WithEnv("STENCIL_BOT_WORKSPACE_TTL_MINUTES", "15", () =>
            Assert.Equal(TimeSpan.FromMinutes(15), BotOptions.FromEnvironment().WorkspaceTtl));
        // Garbage falls back to the default.
        WithEnv("STENCIL_BOT_HTTP_TIMEOUT_SECONDS", "nope", () =>
            Assert.Equal(TimeSpan.FromSeconds(30), BotOptions.FromEnvironment().ServerHttpTimeout));
    }

    private static void WithEnv(string name, string value, Action body)
    {
        string? original = Environment.GetEnvironmentVariable(name);
        try
        {
            Environment.SetEnvironmentVariable(name, value);
            body();
        }
        finally
        {
            Environment.SetEnvironmentVariable(name, original);
        }
    }

    [Theory]
    [InlineData("3", 3)]
    [InlineData("1", 1)]
    [InlineData("0", null)]     // below 1 → default
    [InlineData("-4", null)]    // negative → default
    [InlineData("garbage", null)] // unparseable → default
    [InlineData("", null)]      // unset/blank → default
    public void FromEnvironmentParsesMaxConcurrentCli(string value, int? expected)
    {
        string? original = Environment.GetEnvironmentVariable(MaxConcurrentCliVar);
        try
        {
            Environment.SetEnvironmentVariable(MaxConcurrentCliVar, value);
            int want = expected ?? Math.Max(1, Environment.ProcessorCount);
            Assert.Equal(want, BotOptions.FromEnvironment().MaxConcurrentCli);
        }
        finally
        {
            Environment.SetEnvironmentVariable(MaxConcurrentCliVar, original);
        }
    }
}
