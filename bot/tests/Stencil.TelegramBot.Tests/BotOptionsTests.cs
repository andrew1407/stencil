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
        WithEnv("STENCIL_BOT_HTTP_TIMEOUT_SECONDS", "nope", () =>
            Assert.Equal(TimeSpan.FromSeconds(30), BotOptions.FromEnvironment().ServerHttpTimeout));
    }

    [Fact]
    public void LlmDefaultsFollowTheContract()
    {
        Stencil.TelegramBot.Domain.Llm.LlmOptions llm = new BotOptions().Llm;
        Assert.Equal("ollama", llm.Provider);
        Assert.Equal("http://localhost:11434", llm.BaseUrl);
        Assert.Equal("", llm.Model);
        Assert.Equal("", llm.ApiKey);
        Assert.Null(llm.ServerUrl);
    }

    [Fact]
    public void FromEnvironmentParsesTheStencilLlmKeys()
    {
        // The base-URL default follows the chosen provider (openai-compat → LM Studio's port)…
        WithEnv("STENCIL_LLM_PROVIDER", "openai-compat", () =>
            Assert.Equal("http://localhost:1234/v1", BotOptions.FromEnvironment().Llm.BaseUrl));
        // …unless overridden explicitly.
        WithEnv("STENCIL_LLM_BASE_URL", "http://gpu-box:11434", () =>
            Assert.Equal("http://gpu-box:11434", BotOptions.FromEnvironment().Llm.BaseUrl));
        WithEnv("STENCIL_LLM_MODEL", "llama3.2-vision", () =>
            Assert.Equal("llama3.2-vision", BotOptions.FromEnvironment().Llm.Model));
        WithEnv("STENCIL_LLM_API_KEY", "sk-1", () =>
            Assert.Equal("sk-1", BotOptions.FromEnvironment().Llm.ApiKey));
        WithEnv("STENCIL_LLM_SERVER_URL", "https://stencil.example.com:8090", () =>
            Assert.Equal("https://stencil.example.com:8090", BotOptions.FromEnvironment().Llm.ServerUrl));
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

    // Unlike the CLI cap, 0 is meaningful here: it means unlimited, so it is kept.
    [Theory]
    [InlineData("3", 3)]
    [InlineData("1", 1)]
    [InlineData("0", 0)]
    [InlineData("-4", 8)]       // negative → default
    [InlineData("garbage", 8)]  // unparseable → default
    [InlineData("", 8)]         // unset/blank → default
    public void FromEnvironmentParsesMaxConcurrentLlm(string value, int expected)
    {
        const string name = "STENCIL_BOT_MAX_CONCURRENT_LLM";
        string? original = Environment.GetEnvironmentVariable(name);
        try
        {
            Environment.SetEnvironmentVariable(name, value);
            Assert.Equal(expected, BotOptions.FromEnvironment().MaxConcurrentLlm);
        }
        finally
        {
            Environment.SetEnvironmentVariable(name, original);
        }
    }

    // Fails closed (empty = off for everyone); a typo is dropped rather than widening the list.
    [Theory]
    [InlineData(null, new long[0])]
    [InlineData("", new long[0])]
    [InlineData("   ", new long[0])]
    [InlineData("55", new long[] { 55 })]
    [InlineData("55,91", new long[] { 55, 91 })]
    [InlineData(" 55 , 91 ", new long[] { 55, 91 })]
    [InlineData("55 91;7", new long[] { 55, 91, 7 })]
    [InlineData("55,55", new long[] { 55 })]
    [InlineData("55,oops,91", new long[] { 55, 91 })]
    [InlineData("oops", new long[0])]
    [InlineData("0", new long[0])]
    [InlineData("-1001234", new long[] { -1001234 })]
    public void FromEnvironmentParsesTheAllowlist(string? value, long[] expected)
    {
        const string name = "STENCIL_BOT_ALLOWED_USERS";
        string? original = Environment.GetEnvironmentVariable(name);
        try
        {
            Environment.SetEnvironmentVariable(name, value);
            BotOptions options = BotOptions.FromEnvironment();
            Assert.Equal(expected.OrderBy(x => x), options.AllowedUsers.OrderBy(x => x));
            foreach (long id in expected)
            {
                Assert.True(options.AllowedFor(id));
            }
            Assert.False(options.AllowedFor(123456));
        }
        finally
        {
            Environment.SetEnvironmentVariable(name, original);
        }
    }
}
