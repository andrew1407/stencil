using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests;

/// <summary>
/// CLI discovery for <see cref="StencilCliLocator"/> — a port of <c>mcp/src/locate.rs</c>'s
/// override/missing behaviour. Only the env/override branches are exercised so the suite stays
/// independent of whether a built CLI happens to exist in the checkout.
/// </summary>
public sealed class StencilCliLocatorTests
{
    [Fact]
    public void ExplicitOverrideFileResolves()
    {
        string temp = Path.Combine(Path.GetTempPath(), "stencil-cli-" + Guid.NewGuid().ToString("N"));
        File.WriteAllText(temp, "#!/bin/sh\n");
        try
        {
            Assert.Equal(temp, StencilCliLocator.FindCli(temp));
        }
        finally
        {
            File.Delete(temp);
        }
    }

    [Fact]
    public void StencilCliEnvFileResolves()
    {
        string temp = Path.Combine(Path.GetTempPath(), "stencil-cli-" + Guid.NewGuid().ToString("N"));
        File.WriteAllText(temp, "#!/bin/sh\n");
        string? prior = Environment.GetEnvironmentVariable("STENCIL_CLI");
        try
        {
            Environment.SetEnvironmentVariable("STENCIL_CLI", temp);
            Assert.Equal(temp, StencilCliLocator.FindCli(null));
        }
        finally
        {
            Environment.SetEnvironmentVariable("STENCIL_CLI", prior);
            File.Delete(temp);
        }
    }

    // The chat is told the engine is unavailable; the env var and the bad path are the
    // operator's to read in the log, so they ride OperatorDetail instead of the message.
    [Fact]
    public void NonFileOverrideThrowsWithThePathInTheOperatorDetailOnly()
    {
        string missing = Path.Combine(Path.GetTempPath(), "stencil-missing-" + Guid.NewGuid().ToString("N"));
        StencilCliException ex = Assert.Throws<StencilCliException>(() => StencilCliLocator.FindCli(missing));
        Assert.Equal(StencilCliLocator.UNAVAILABLE_MESSAGE, ex.Message);
        Assert.DoesNotContain("STENCIL_", ex.Message);
        Assert.DoesNotContain(missing, ex.Message);
        Assert.Contains("not a file", ex.OperatorDetail);
        Assert.Contains(missing, ex.OperatorDetail);
    }

    [Fact]
    public void UnavailableMessageNamesNoDeploymentDetail()
    {
        Assert.DoesNotContain("STENCIL_", StencilCliLocator.UNAVAILABLE_MESSAGE);
        Assert.DoesNotContain("zig", StencilCliLocator.UNAVAILABLE_MESSAGE);
        Assert.DoesNotContain("/", StencilCliLocator.UNAVAILABLE_MESSAGE);
    }

    [Fact]
    public void MissingMessageShape()
    {
        Assert.Contains("could not find the `stencil` CLI", StencilCliLocator.MISSING_MESSAGE);
        Assert.Contains("STENCIL_CLI", StencilCliLocator.MISSING_MESSAGE);
        Assert.Contains("zig build", StencilCliLocator.MISSING_MESSAGE);
    }
}
