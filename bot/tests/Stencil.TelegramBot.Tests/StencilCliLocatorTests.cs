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

    [Fact]
    public void NonFileOverrideThrows()
    {
        string missing = Path.Combine(Path.GetTempPath(), "stencil-missing-" + Guid.NewGuid().ToString("N"));
        StencilCliException ex = Assert.Throws<StencilCliException>(() => StencilCliLocator.FindCli(missing));
        Assert.Contains("not a file", ex.Message);
        Assert.Contains(missing, ex.Message);
    }

    [Fact]
    public void MissingMessageShape()
    {
        Assert.Contains("could not find the `stencil` CLI", StencilCliLocator.MissingMessage);
        Assert.Contains("STENCIL_CLI", StencilCliLocator.MissingMessage);
        Assert.Contains("zig build", StencilCliLocator.MissingMessage);
    }
}
