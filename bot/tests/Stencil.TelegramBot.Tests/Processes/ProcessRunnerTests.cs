using Stencil.TelegramBot.Infrastructure.Processes;

namespace Stencil.TelegramBot.Tests.Processes;

/// <summary>The child-process scaffold: a child (the CLI, ffmpeg) inherits only the allowlisted environment, never the bot's secrets, is fed its stdin whole, and its output is kept up to a cap while the rest is drained.</summary>
public sealed class ProcessRunnerTests
{
    [Fact]
    public void Should_Keep_Only_The_Allowlist_When_Restricting_A_Child_Environment()
    {
        Dictionary<string, string?> environment = new()
        {
            ["TELEGRAM_BOT_TOKEN"] = "1:x",
            ["REDIS_URL"] = "redis://h",
            ["STENCIL_LLM_API_KEY"] = "sk",
            ["STENCIL_LLM_SERVER_TOKEN"] = "t",
            ["STENCIL_BOT_ALLOWED_USERS"] = "1",
            ["AWS_SECRET_ACCESS_KEY"] = "s",
            ["PATH"] = "/usr/bin",
            ["HOME"] = "/home/bot",
            ["TMPDIR"] = "/tmp",
            ["LC_ALL"] = "C.UTF-8",
            ["https_proxy"] = "http://proxy:3128",
            ["NO_COLOR"] = "1",
        };

        ChildEnvironment.Restrict(environment);

        Assert.Equal(
            ["HOME", "LC_ALL", "NO_COLOR", "PATH", "TMPDIR", "https_proxy"],
            environment.Keys.Order(StringComparer.Ordinal));
    }

    [Fact]
    public async Task Should_Not_Hand_The_Bots_Own_Environment_To_A_Child()
    {
        const string env = "/usr/bin/env";
        if (!File.Exists(env))
        {
            return; // no POSIX env(1) to ask
        }
        string name = $"STENCIL_LLM_PROBE_{Guid.NewGuid():N}";
        Environment.SetEnvironmentVariable(name, "leaked");
        try
        {
            ProcessOutcome outcome = await ProcessRunner.RunAsync(
                env, [], TimeSpan.FromSeconds(30), CancellationToken.None,
                new Dictionary<string, string> { ["NO_COLOR"] = "1" });

            ProcessCompleted done = Assert.IsType<ProcessCompleted>(outcome);
            Assert.DoesNotContain(name, done.Stdout);
            Assert.Contains("NO_COLOR=1", done.Stdout); // the caller's own additions still ride
            Assert.Contains("PATH=", done.Stdout);
        }
        finally
        {
            Environment.SetEnvironmentVariable(name, null);
        }
    }

    /// <summary>A model reply reaches --plan-check on stdin: UTF-8 with no byte-order mark, then EOF.</summary>
    [Fact]
    public async Task Should_Feed_Stdin_As_Utf8_Without_A_Bom_And_Close_It()
    {
        const string cat = "/bin/cat";
        if (!File.Exists(cat))
        {
            return; // no POSIX cat(1) to echo it
        }
        ProcessOutcome outcome = await ProcessRunner.RunAsync(
            cat, [], TimeSpan.FromSeconds(30), CancellationToken.None, stdin: "{\"reply\":\"héllo — §1\"}");

        ProcessCompleted done = Assert.IsType<ProcessCompleted>(outcome);
        Assert.Equal(0, done.ExitCode);
        Assert.Equal("{\"reply\":\"héllo — §1\"}", done.Stdout);
    }

    [Fact]
    public async Task Should_Keep_The_Head_And_Drain_The_Rest_When_Reading_Past_The_Cap()
    {
        StringReader reader = new(new string('a', 10) + new string('b', 50_000));

        string kept = await ProcessRunner.ReadCappedAsync(reader, 10, CancellationToken.None);

        Assert.Equal(new string('a', 10), kept);
        Assert.Equal(-1, reader.Peek()); // drained, so a child never blocks on a full pipe
    }
}
