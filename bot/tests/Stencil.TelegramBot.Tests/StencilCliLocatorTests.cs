using Stencil.TelegramBot.Domain.Exceptions;
using Stencil.TelegramBot.Infrastructure.Cli;

namespace Stencil.TelegramBot.Tests;

/// <summary>CLI discovery, a port of <c>mcp/src/locate.rs</c>'s override/missing behaviour. Only the env/override branches are exercised, so the suite stays independent of whether a built CLI exists in the checkout.</summary>
public sealed class StencilCliLocatorTests
{
    [Fact]
    public void Should_Resolve_An_Explicit_Override_File()
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
    public void Should_Resolve_The_Stencil_Cli_Env_File()
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
    public void Should_Throw_With_The_Path_In_The_Operator_Detail_Only_For_A_Non_File_Override()
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
    public void Should_Name_No_Deployment_Detail_In_The_Unavailable_Message()
    {
        Assert.DoesNotContain("STENCIL_", StencilCliLocator.UNAVAILABLE_MESSAGE);
        Assert.DoesNotContain("zig", StencilCliLocator.UNAVAILABLE_MESSAGE);
        Assert.DoesNotContain("/", StencilCliLocator.UNAVAILABLE_MESSAGE);
    }

    [Fact]
    public void Should_Name_The_Cli_The_Env_Knob_And_The_Build_Step_In_The_Missing_Message()
    {
        Assert.Contains("could not find the `stencil` CLI", StencilCliLocator.MISSING_MESSAGE);
        Assert.Contains("STENCIL_CLI", StencilCliLocator.MISSING_MESSAGE);
        Assert.Contains("zig build", StencilCliLocator.MISSING_MESSAGE);
    }
}
